#include "common/audit/AuditClient.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>

namespace common::audit {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

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

}  // namespace

struct Client::Impl {
  Config config;
  bool ready = false;
  Endpoint endpoint;
  std::string host_header;
  ssl::context ssl_context{ssl::context::tls_client};
  std::mutex mutex;

  bool post(const std::string& body, const std::string& request_id,
            int& status, std::string& error);
};

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

  impl_->ready = true;
}

Client::~Client() = default;

bool Client::enabled() const { return impl_ != nullptr && impl_->ready; }

const std::string& Client::serviceName() const {
  return impl_->config.service_name;
}

bool Client::emit(const Event& event) {
  if (!enabled()) return false;

  if (event.event_type.empty() ||
      (event.outcome != "success" && event.outcome != "failure")) {
    std::cerr << "[audit-emit] refusing invalid event type/outcome\n";
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);

  const std::string id = uuidV4();
  const std::string payload =
      makePayload(impl_->config, event, id).dump();

  std::string error;
  int status = 0;

  for (int attempt = 1; attempt <= impl_->config.max_attempts; ++attempt) {
    if (attempt > 1) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(100 * (attempt - 1)));
    }

    status = 0;
    error.clear();

    if (!impl_->post(payload, id, status, error)) {
      // Transport failure: retry with the same event_id.
      continue;
    }

    if (status == 200 || status == 201) return true;

    if (status == 409 || status >= 500) {
      // Chain contention or the audit store is temporarily unavailable.
      continue;
    }

    // 4xx: retrying will not help (bad config, forbidden identity, ...).
    std::cerr << "[audit-emit] rejected event_id=" << id
              << " type=" << event.event_type << " status=" << status
              << " body=" << error << '\n';
    return false;
  }

  std::cerr << "[audit-emit] failed event_id=" << id
            << " type=" << event.event_type
            << " service=" << impl_->config.service_name
            << " status=" << status << " error=" << error << '\n';
  return false;
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
  config.max_attempts = std::clamp(envInt("AUDIT_MAX_ATTEMPTS", 2), 1, 5);
  return config;
}

}  // namespace common::audit
