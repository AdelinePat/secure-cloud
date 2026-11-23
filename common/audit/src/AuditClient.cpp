#include "common/audit/AuditClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace common::audit {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
namespace fs = std::filesystem;
using tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;

constexpr const char* kDeadDir = "dead";
constexpr const char* kLockFile = ".lock";
constexpr const char* kSpoolExt = ".json";
constexpr const char* kTempExt = ".tmp";

std::string env(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') return fallback;
  return value;
}

int envInt(const char* name, int fallback) {
  const std::string raw = env(name, "");
  if (raw.empty()) return fallback;
  try {
    return std::stoi(raw);
  } catch (const std::exception&) {
    std::cerr << "[audit-emit] " << name << " must be an integer, using "
              << fallback << '\n';
    return fallback;
  }
}

bool envBool(const char* name, bool fallback) {
  std::string raw = env(name, "");
  if (raw.empty()) return fallback;
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") return true;
  if (raw == "0" || raw == "false" || raw == "no" || raw == "off") return false;
  std::cerr << "[audit-emit] " << name << " must be a boolean, using "
            << (fallback ? "true" : "false") << '\n';
  return fallback;
}

// RFC 4122 v4 id used as the caller-supplied idempotency key.
std::string uuidV4() {
  static thread_local std::mt19937_64 generator{std::random_device{}()};
  std::uniform_int_distribution<int> distribution(0, 255);

  std::array<unsigned char, 16> bytes{};
  for (auto& byte : bytes) {
    byte = static_cast<unsigned char>(distribution(generator));
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);

  static constexpr char kHex[] = "0123456789abcdef";
  std::string id;
  id.reserve(36);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) id += '-';
    id += kHex[bytes[i] >> 4];
    id += kHex[bytes[i] & 0x0F];
  }
  return id;
}

std::string nowIso8601() {
  using namespace std::chrono;
  const auto now = system_clock::now().time_since_epoch();
  const auto millis = duration_cast<milliseconds>(now).count();
  const std::time_t seconds = static_cast<std::time_t>(millis / 1000);
  const int remainder = static_cast<int>(millis % 1000);

  std::tm utc{};
  gmtime_r(&seconds, &utc);

  char buffer[32];
  const int written = std::snprintf(
      buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
      utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
      utc.tm_min, utc.tm_sec, remainder);
  return std::string(buffer, static_cast<std::size_t>(written));
}

nlohmann::json makePayload(const Config& config, const Event& event,
                           const std::string& id) {
  nlohmann::json body;
  body["event_id"] = id;
  body["event_type"] = event.event_type;
  body["service_name"] = config.service_name;
  body["outcome"] = event.outcome;
  // Captured now, not at delivery time: a spooled event may be sent much later.
  body["occurred_at"] = event.occurred_at.value_or(nowIso8601());

  if (event.actor_id.has_value() && !event.actor_id->empty()) {
    body["actor_id"] = *event.actor_id;
  }
  if (event.target_id.has_value() && !event.target_id->empty()) {
    body["target_id"] = *event.target_id;
  }
  if (event.metadata.is_object() && !event.metadata.empty()) {
    body["metadata"] = event.metadata;
  }
  return body;
}

struct Endpoint {
  std::string scheme;
  std::string host;
  std::string port;
  std::string path;  // optional base path, no trailing slash
};

// Supports http(s)://host[:port][/base]. Userinfo and IPv6 literals are not
// needed by the Compose network, so they are rejected explicitly.
bool parseUrl(const std::string& url, Endpoint& endpoint) {
  const auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) return false;

  endpoint.scheme = url.substr(0, scheme_end);
  if (endpoint.scheme != "https" && endpoint.scheme != "http") return false;

  const std::size_t authority_start = scheme_end + 3;
  const auto path_start = url.find('/', authority_start);
  const std::string authority =
      url.substr(authority_start, path_start - authority_start);

  endpoint.path = path_start == std::string::npos
                      ? std::string()
                      : url.substr(path_start);
  while (!endpoint.path.empty() && endpoint.path.back() == '/') {
    endpoint.path.pop_back();
  }

  if (authority.empty() || authority.find('@') != std::string::npos ||
      authority.find('[') != std::string::npos) {
    return false;
  }

  const auto colon = authority.rfind(':');
  if (colon != std::string::npos) {
    endpoint.host = authority.substr(0, colon);
    endpoint.port = authority.substr(colon + 1);
  } else {
    endpoint.host = authority;
    endpoint.port = endpoint.scheme == "https" ? "443" : "80";
  }

  return !endpoint.host.empty() && !endpoint.port.empty();
}

// ── spool file helpers ─────────────────────────────────────────────────────

std::string errnoText() { return std::strerror(errno); }

bool fsyncPath(const fs::path& path, int flags) {
  const int fd = ::open(path.c_str(), flags | O_CLOEXEC);
  if (fd < 0) return false;
  const bool ok = ::fsync(fd) == 0;
  ::close(fd);
  return ok;
}

// Writes `content` to `target` atomically and durably: a crash leaves either
// no file or the complete file, never a truncated one.
bool writeFileDurable(const fs::path& target, const std::string& content,
                      std::string& error) {
  const fs::path temp = fs::path(target).concat(kTempExt);
  const int fd = ::open(temp.c_str(),
                        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (fd < 0) {
    error = "open " + temp.string() + ": " + errnoText();
    return false;
  }

  const char* data = content.data();
  std::size_t left = content.size();
  while (left > 0) {
    const ssize_t written = ::write(fd, data, left);
    if (written < 0) {
      if (errno == EINTR) continue;
      error = "write " + temp.string() + ": " + errnoText();
      ::close(fd);
      ::unlink(temp.c_str());
      return false;
    }
    data += written;
    left -= static_cast<std::size_t>(written);
  }

  if (::fsync(fd) != 0) {
    error = "fsync " + temp.string() + ": " + errnoText();
    ::close(fd);
    ::unlink(temp.c_str());
    return false;
  }
  ::close(fd);

  if (::rename(temp.c_str(), target.c_str()) != 0) {
    error = "rename " + temp.string() + ": " + errnoText();
    ::unlink(temp.c_str());
    return false;
  }

  // Persist the directory entry created by rename().
  if (!fsyncPath(target.parent_path(), O_RDONLY | O_DIRECTORY)) {
    error = "fsync dir " + target.parent_path().string() + ": " + errnoText();
    // The file exists and is complete; only its persistence is uncertain.
  }
  return true;
}

// Spool names are "<20-digit sequence>-<event_id>.json" so that a plain
// lexicographic sort restores emit order after a restart.
std::string spoolFileName(std::uint64_t sequence, const std::string& id) {
  char prefix[32];
  std::snprintf(prefix, sizeof(prefix), "%020llu",
                static_cast<unsigned long long>(sequence));
  return std::string(prefix) + '-' + id + kSpoolExt;
}

std::optional<std::uint64_t> spoolSequence(const std::string& name) {
  if (name.size() < 21 || name[20] != '-') return std::nullopt;
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 20; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(name[i]))) return std::nullopt;
    value = value * 10 + static_cast<std::uint64_t>(name[i] - '0');
  }
  return value;
}

enum class Delivery { Delivered, Retry, Rejected };

}  // namespace

struct Client::Impl {
  struct Pending {
    std::string id;
    std::string event_type;
    std::string payload;
    fs::path file;  // empty in memory mode
  };

  Config config;
  bool ready = false;
  bool durable = false;
  Endpoint endpoint;
  std::string host_header;
  ssl::context ssl_context{ssl::context::tls_client};

  // Spool state. spool_mutex serialises emit() so the spool sequence and the
  // in-memory queue order always agree.
  fs::path spool_dir;
  fs::path dead_dir;
  int lock_fd = -1;
  std::uint64_t next_sequence = 1;
  std::mutex spool_mutex;
  std::size_t dropped = 0;

  // Queue shared with the worker.
  mutable std::mutex mutex;
  std::condition_variable wake;  // worker: new event or stop
  std::condition_variable idle;  // flush(): queue drained
  std::deque<Pending> queue;
  bool stopping = false;
  Clock::time_point drain_deadline{};
  std::thread worker;

  ~Impl();

  void openSpool();
  void recoverSpool();
  void run();
  Delivery deliver(const Pending& item, std::string& detail);
  void finish(const Pending& item, Delivery result, const std::string& detail);

  bool post(const std::string& body, const std::string& request_id,
            int& status, std::string& error);
};

Client::Impl::~Impl() {
  if (lock_fd >= 0) ::close(lock_fd);
}

void Client::Impl::openSpool() {
  if (config.spool_dir.empty()) {
    std::cerr << "[audit-emit] AUDIT_SPOOL_DIR not set; queue is in memory "
                 "only, pending events are lost on shutdown\n";
    return;
  }

  try {
    spool_dir = fs::path(config.spool_dir);
    dead_dir = spool_dir / kDeadDir;
    fs::create_directories(dead_dir);
    fs::permissions(spool_dir, fs::perms::owner_all, fs::perm_options::replace);
  } catch (const std::exception& exception) {
    std::cerr << "[audit-emit] cannot create spool " << config.spool_dir
              << " (" << exception.what() << "); falling back to memory\n";
    return;
  }

  const fs::path lock_path = spool_dir / kLockFile;
  lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (lock_fd < 0) {
    std::cerr << "[audit-emit] cannot open " << lock_path << " ("
              << errnoText() << "); falling back to memory\n";
    return;
  }
  if (::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    std::cerr << "[audit-emit] spool " << spool_dir
              << " is used by another process; falling back to memory\n";
    ::close(lock_fd);
    lock_fd = -1;
    return;
  }

  durable = true;
  recoverSpool();
}

// Loads events left by a previous process, in their original order.
void Client::Impl::recoverSpool() {
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(spool_dir)) {
    if (!entry.is_regular_file()) continue;
    const fs::path& path = entry.path();
    const std::string name = path.filename().string();

    if (path.extension() == kTempExt) {
      // Interrupted write: emit() never returned true for it.
      std::error_code ignored;
      fs::remove(path, ignored);
      continue;
    }
    if (path.extension() == kSpoolExt && spoolSequence(name)) {
      files.push_back(path);
    }
  }
  std::sort(files.begin(), files.end());

  for (const auto& path : files) {
    const std::string name = path.filename().string();
    next_sequence = std::max(next_sequence, *spoolSequence(name) + 1);

    std::ifstream input(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
    const auto json = nlohmann::json::parse(content, nullptr, false);

    if (json.is_discarded() || !json.is_object() ||
        !json.contains("event_id") || !json["event_id"].is_string()) {
      std::error_code ignored;
      fs::rename(path, dead_dir / path.filename(), ignored);
      std::cerr << "[audit-emit] unreadable spool file " << name
                << " moved to dead/\n";
      continue;
    }

    queue.push_back(Pending{
        json["event_id"].get<std::string>(),
        json.value("event_type", std::string()),
        std::move(content),
        path,
    });
  }

  if (!queue.empty()) {
    std::cerr << "[audit-emit] recovered " << queue.size()
              << " pending event(s) from " << spool_dir << '\n';
  }
}

Client::Client(Config config) : impl_(std::make_unique<Impl>()) {
  impl_->config = std::move(config);

  if (!impl_->config.enabled) return;

  if (impl_->config.service_name.empty()) {
    std::cerr << "[audit-emit] AUDIT_SERVICE_NAME is empty; emitter disabled\n";
    return;
  }

  if (!parseUrl(impl_->config.url, impl_->endpoint)) {
    std::cerr << "[audit-emit] invalid AUDIT_URL '" << impl_->config.url
              << "'; emitter disabled\n";
    return;
  }

  impl_->host_header =
      impl_->endpoint.host + ':' + impl_->endpoint.port;

  if (impl_->endpoint.scheme == "https") {
    try {
      impl_->ssl_context.set_options(
          ssl::context::default_workarounds | ssl::context::no_sslv2 |
          ssl::context::no_sslv3);
      impl_->ssl_context.set_verify_mode(ssl::verify_peer);
      impl_->ssl_context.load_verify_file(impl_->config.ca_path);
      impl_->ssl_context.use_certificate_chain_file(impl_->config.cert_path);
      impl_->ssl_context.use_private_key_file(impl_->config.key_path,
                                              ssl::context::pem);
    } catch (const std::exception& exception) {
      std::cerr << "[audit-emit] TLS setup failed (" << exception.what()
                << "); emitter disabled\n";
      return;
    }
  }

  try {
    impl_->openSpool();
  } catch (const std::exception& exception) {
    // Recovery failed half-way: keep what was loaded, stay durable for new
    // events if the directory itself is usable.
    std::cerr << "[audit-emit] spool recovery error (" << exception.what()
              << ")\n";
  }

  impl_->ready = true;
  impl_->worker = std::thread([impl = impl_.get()] { impl->run(); });
}

Client::~Client() {
  if (!impl_ || !impl_->worker.joinable()) return;

  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stopping = true;
    impl_->drain_deadline =
        Clock::now() +
        std::chrono::milliseconds(impl_->config.shutdown_drain_ms);
  }
  impl_->wake.notify_all();
  impl_->worker.join();

  const std::size_t left = impl_->queue.size();
  if (left > 0) {
    std::cerr << "[audit-emit] " << left << " event(s) not delivered at "
              << "shutdown; "
              << (impl_->durable ? "kept in the spool for the next start"
                                 : "LOST (no spool directory)")
              << '\n';
  }
}

bool Client::enabled() const { return impl_ != nullptr && impl_->ready; }

bool Client::durable() const { return enabled() && impl_->durable; }

const std::string& Client::serviceName() const {
  return impl_->config.service_name;
}

std::size_t Client::pending() const {
  if (!enabled()) return 0;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->queue.size();
}

bool Client::flush(std::chrono::milliseconds timeout) {
  if (!enabled()) return true;
  std::unique_lock<std::mutex> lock(impl_->mutex);
  return impl_->idle.wait_for(lock, timeout,
                              [this] { return impl_->queue.empty(); });
}

bool Client::emit(const Event& event) {
  if (!enabled()) return false;

  if (event.event_type.empty() ||
      (event.outcome != "success" && event.outcome != "failure")) {
    std::cerr << "[audit-emit] refusing invalid event type/outcome\n";
    return false;
  }

  try {
    Impl::Pending item;
    item.id = uuidV4();
    item.event_type = event.event_type;
    item.payload = makePayload(impl_->config, event, item.id).dump();

    std::lock_guard<std::mutex> spool_lock(impl_->spool_mutex);

    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      if (impl_->queue.size() >= impl_->config.max_pending) {
        ++impl_->dropped;
        std::cerr << "[audit-emit] queue full (" << impl_->queue.size()
                  << " pending), DROPPED event type=" << event.event_type
                  << " total_dropped=" << impl_->dropped << '\n';
        return false;
      }
    }

    if (impl_->durable) {
      const fs::path file =
          impl_->spool_dir /
          spoolFileName(impl_->next_sequence++, item.id);
      std::string error;
      if (!writeFileDurable(file, item.payload, error)) {
        // Disk problem: still try to deliver it from memory.
        std::cerr << "[audit-emit] spool write failed (" << error
                  << "); event_id=" << item.id << " queued in memory only\n";
      } else {
        item.file = file;
      }
    }

    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      impl_->queue.push_back(std::move(item));
    }
    impl_->wake.notify_one();
    return true;
  } catch (const std::exception& exception) {
    std::cerr << "[audit-emit] emit failed (" << exception.what() << ")\n";
    return false;
  }
}

// ── worker ─────────────────────────────────────────────────────────────────

void Client::Impl::run() {
  auto backoff = std::chrono::milliseconds(config.retry_initial_ms);
  bool failing = false;

  std::unique_lock<std::mutex> lock(mutex);
  for (;;) {
    wake.wait(lock, [this] { return stopping || !queue.empty(); });
    if (queue.empty()) break;  // stopping, nothing left
    if (stopping && Clock::now() >= drain_deadline) break;

    // Only this thread pops, so the head stays valid while unlocked.
    const Pending item = queue.front();
    lock.unlock();

    std::string detail;
    const Delivery result = deliver(item, detail);

    if (result != Delivery::Retry) {
      finish(item, result, detail);
      lock.lock();
      queue.pop_front();
      if (queue.empty()) idle.notify_all();

      if (failing) {
        failing = false;
        std::cerr << "[audit-emit] audit service reachable again; "
                  << queue.size() << " event(s) still pending\n";
      }
      backoff = std::chrono::milliseconds(config.retry_initial_ms);
      continue;
    }

    lock.lock();
    if (!failing) {
      failing = true;
      std::cerr << "[audit-emit] delivery failed (" << detail << "); "
                << queue.size() << " event(s) pending, retrying"
                << (durable ? " from the spool" : " from memory") << '\n';
    }
    if (stopping) break;  // keep it for the next start

    wake.wait_for(lock, backoff, [this] { return stopping; });
    backoff = std::min(backoff * 2,
                       std::chrono::milliseconds(config.retry_max_ms));
  }
}

Delivery Client::Impl::deliver(const Pending& item, std::string& detail) {
  int status = 0;
  std::string error;

  if (!post(item.payload, item.id, status, error)) {
    detail = "transport: " + error;
    return Delivery::Retry;
  }
  if (status == 200 || status == 201) return Delivery::Delivered;

  detail = "status " + std::to_string(status) + ": " + error;
  if (status == 408 || status == 409 || status == 429 || status >= 500) {
    return Delivery::Retry;
  }
  return Delivery::Rejected;
}

void Client::Impl::finish(const Pending& item, Delivery result,
                          const std::string& detail) {
  if (result == Delivery::Rejected) {
    std::cerr << "[audit-emit] rejected event_id=" << item.id
              << " type=" << item.event_type << " (" << detail << ")"
              << (item.file.empty() ? "; dropped" : "; moved to dead/")
              << '\n';
  }
  if (item.file.empty()) return;

  std::error_code error;
  if (result == Delivery::Delivered) {
    fs::remove(item.file, error);
  } else {
    fs::rename(item.file, dead_dir / item.file.filename(), error);
  }
  if (error) {
    // Worst case the event is re-sent after a restart: idempotent server-side.
    std::cerr << "[audit-emit] spool cleanup failed for " << item.file
              << " (" << error.message() << ")\n";
  }
}

bool Client::Impl::post(const std::string& body, const std::string& request_id,
                        int& status, std::string& error) {
  try {
    asio::io_context io_context;
    tcp::resolver resolver(io_context);
    const auto results = resolver.resolve(endpoint.host, endpoint.port);

    http::request<http::string_body> request{
        http::verb::post,
        endpoint.path.empty() ? "/audit/events"
                              : endpoint.path + "/audit/events",
        11};
    request.set(http::field::host, host_header);
    request.set(http::field::user_agent, "securechat-audit-emitter/1.0");
    request.set(http::field::content_type, "application/json");
    // Correlate the audit service's request log with this event id.
    request.set("X-Request-Id", request_id);
    request.body() = body;
    request.prepare_payload();

    const auto timeout = std::chrono::milliseconds(config.timeout_ms);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;

    if (endpoint.scheme == "https") {
      beast::ssl_stream<beast::tcp_stream> stream(io_context, ssl_context);
      beast::get_lowest_layer(stream).expires_after(timeout);
      beast::get_lowest_layer(stream).connect(results);

      if (SSL_set_tlsext_host_name(stream.native_handle(),
                                   endpoint.host.c_str()) != 1) {
        error = "could not set TLS SNI";
        return false;
      }
      stream.set_verify_callback(ssl::host_name_verification(endpoint.host));
      stream.handshake(ssl::stream_base::client);

      beast::get_lowest_layer(stream).expires_after(timeout);
      http::write(stream, request);

      beast::get_lowest_layer(stream).expires_after(timeout);
      http::read(stream, buffer, response);

      beast::error_code ignored;
      stream.shutdown(ignored);
    } else {
      beast::tcp_stream stream(io_context);
      stream.expires_after(timeout);
      stream.connect(results);

      http::write(stream, request);

      stream.expires_after(timeout);
      http::read(stream, buffer, response);

      beast::error_code ignored;
      stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    }

    status = response.result_int();
    if (status != 200 && status != 201) {
      error = response.body().substr(0, 300);
    }
    return true;
  } catch (const boost::system::system_error& exception) {
    // what() embeds the Boost source location; the message alone is enough.
    error = exception.code().message();
    return false;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}

Config Config::fromEnv(std::string default_service_name) {
  Config config;
  config.enabled = envBool("AUDIT_ENABLED", false);
  config.url = env("AUDIT_URL", config.url);
  config.service_name = env("AUDIT_SERVICE_NAME", default_service_name);

  const std::string cert_dir =
      "/app/certs/" + config.service_name + '/' + config.service_name;
  config.cert_path = env("AUDIT_TLS_CERT", cert_dir + ".crt");
  config.key_path = env("AUDIT_TLS_KEY", cert_dir + ".key");
  config.ca_path = env("AUDIT_TLS_CA", "/app/certs/ca/ca.crt");

  config.timeout_ms = std::clamp(envInt("AUDIT_TIMEOUT_MS", 750), 50, 10000);

  config.spool_dir = env("AUDIT_SPOOL_DIR", "");
  config.max_pending = static_cast<std::size_t>(
      std::clamp(envInt("AUDIT_SPOOL_MAX_EVENTS", 10000), 1, 1000000));
  config.retry_initial_ms =
      std::clamp(envInt("AUDIT_RETRY_INITIAL_MS", 200), 10, 60000);
  config.retry_max_ms = std::clamp(envInt("AUDIT_RETRY_MAX_MS", 30000),
                                   config.retry_initial_ms, 600000);
  config.shutdown_drain_ms =
      std::clamp(envInt("AUDIT_SHUTDOWN_DRAIN_MS", 2000), 0, 60000);
  return config;
}

}  // namespace common::audit
