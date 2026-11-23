#include "common/audit/AuditClient.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace fs = std::filesystem;
using tcp = asio::ip::tcp;
using json = nlohmann::json;
using namespace std::chrono_literals;

// Minimal plain-HTTP stand-in for the audit service. The handler returns the
// status for each request; every request body is recorded in `seen`, the ones
// answered 2xx also in `stored`.
class FakeAuditServer {
 public:
  using Handler = std::function<int(const json&)>;

  explicit FakeAuditServer(Handler handler)
      : handler_(std::move(handler)),
        acceptor_(io_, {asio::ip::make_address("127.0.0.1"), 0}) {
    port_ = acceptor_.local_endpoint().port();
    accept();
    thread_ = std::thread([this] { io_.run(); });
  }

  ~FakeAuditServer() {
    io_.stop();
    thread_.join();
  }

  std::string url() const {
    return "http://127.0.0.1:" + std::to_string(port_);
  }

  std::vector<json> seen() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seen_;
  }

  std::vector<json> stored() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stored_;
  }

 private:
  void accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (ec) return;
      serve(std::move(socket));
      accept();
    });
  }

  void serve(tcp::socket socket) {
    beast::error_code ec;
    beast::flat_buffer buffer;
    http::request<http::string_body> request;
    http::read(socket, buffer, request, ec);
    if (ec) return;

    const json body = json::parse(request.body(), nullptr, false);
    const int status = handler_(body);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      seen_.push_back(body);
      if (status / 100 == 2) stored_.push_back(body);
    }

    http::response<http::string_body> response{
        static_cast<http::status>(status), 11};
    response.set(http::field::content_type, "application/json");
    response.body() = "{}";
    response.prepare_payload();
    http::write(socket, response, ec);
    socket.shutdown(tcp::socket::shutdown_both, ec);
  }

  Handler handler_;
  asio::io_context io_;
  tcp::acceptor acceptor_;
  unsigned short port_ = 0;
  std::thread thread_;
  mutable std::mutex mutex_;
  std::vector<json> seen_;
  std::vector<json> stored_;
};

// Nothing listens on port 1: connections are refused immediately.
constexpr const char* kUnreachableUrl = "http://127.0.0.1:1";

class AuditClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::random_device random;
    spool_ = fs::temp_directory_path() /
             ("audit-client-test-" + std::to_string(random()));
    fs::create_directories(spool_);
  }

  void TearDown() override {
    std::error_code ignored;
    fs::remove_all(spool_, ignored);
  }

  common::audit::Config config(const std::string& url,
                               bool durable = true) const {
    common::audit::Config config;
    config.enabled = true;
    config.service_name = "test";
    config.url = url;
    config.timeout_ms = 500;
    config.spool_dir = durable ? spool_.string() : std::string();
    config.retry_initial_ms = 10;
    config.retry_max_ms = 40;
    config.shutdown_drain_ms = 0;
    return config;
  }

  static common::audit::Event event(const std::string& type) {
    common::audit::Event event;
    event.event_type = type;
    event.actor_id = "alice";
    event.metadata = {{"k", type}};
    return event;
  }

  std::size_t countFiles(const fs::path& dir) const {
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
      if (entry.path().extension() == ".json") ++count;
    }
    return count;
  }

  fs::path spool_;
};

TEST_F(AuditClientTest, DeliversInOrderAndClearsSpool) {
  FakeAuditServer server([](const json&) { return 201; });
  common::audit::Client client(config(server.url()));
  ASSERT_TRUE(client.enabled());
  ASSERT_TRUE(client.durable());

  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(client.emit(event("e" + std::to_string(i))));
  }
  ASSERT_TRUE(client.flush(5s));

  const auto stored = server.stored();
  ASSERT_EQ(stored.size(), 5u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(stored[i]["event_type"], "e" + std::to_string(i));
    EXPECT_EQ(stored[i]["service_name"], "test");
    EXPECT_EQ(stored[i]["actor_id"], "alice");
    EXPECT_TRUE(stored[i].contains("occurred_at"));
    EXPECT_FALSE(stored[i]["event_id"].get<std::string>().empty());
  }
  EXPECT_EQ(countFiles(spool_), 0u);
  EXPECT_EQ(client.pending(), 0u);
}

TEST_F(AuditClientTest, RetriesTransientFailuresWithTheSameEventId) {
  std::atomic<int> calls{0};
  FakeAuditServer server([&](const json&) {
    const int call = ++calls;
    if (call == 1) return 503;
    if (call == 2) return 409;
    return 201;
  });
  common::audit::Client client(config(server.url()));

  ASSERT_TRUE(client.emit(event("login_failed")));
  ASSERT_TRUE(client.flush(5s));

  const auto seen = server.seen();
  ASSERT_EQ(seen.size(), 3u);
  EXPECT_EQ(seen[0]["event_id"], seen[1]["event_id"]);
  EXPECT_EQ(seen[1]["event_id"], seen[2]["event_id"]);
  EXPECT_EQ(server.stored().size(), 1u);
}

TEST_F(AuditClientTest, RejectedEventGoesToDeadLetterWithoutBlocking) {
  FakeAuditServer server([](const json& body) {
    return body["event_type"] == "bad" ? 400 : 201;
  });
  common::audit::Client client(config(server.url()));

  ASSERT_TRUE(client.emit(event("bad")));
  ASSERT_TRUE(client.emit(event("good")));
  ASSERT_TRUE(client.flush(5s));

  const auto stored = server.stored();
  ASSERT_EQ(stored.size(), 1u);
  EXPECT_EQ(stored[0]["event_type"], "good");
  EXPECT_EQ(countFiles(spool_), 0u);
  EXPECT_EQ(countFiles(spool_ / "dead"), 1u);
}

TEST_F(AuditClientTest, SpooledEventsSurviveARestart) {
  std::vector<std::string> ids;
  {
    common::audit::Client offline(config(kUnreachableUrl));
    ASSERT_TRUE(offline.durable());
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(offline.emit(event("e" + std::to_string(i))));
    }
    // Audit is down: nothing can be delivered, everything stays on disk.
    EXPECT_FALSE(offline.flush(100ms));
    EXPECT_EQ(offline.pending(), 3u);
  }  // "process stops"
  EXPECT_EQ(countFiles(spool_), 3u);

  FakeAuditServer server([](const json&) { return 201; });
  common::audit::Client restarted(config(server.url()));
  ASSERT_TRUE(restarted.flush(5s));

  const auto stored = server.stored();
  ASSERT_EQ(stored.size(), 3u);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(stored[i]["event_type"], "e" + std::to_string(i));
  }
  EXPECT_EQ(countFiles(spool_), 0u);

  // New events are ordered after the recovered ones.
  ASSERT_TRUE(restarted.emit(event("after_restart")));
  ASSERT_TRUE(restarted.flush(5s));
  EXPECT_EQ(server.stored().back()["event_type"], "after_restart");
}

TEST_F(AuditClientTest, RefusesEventsWhenTheQueueIsFull) {
  auto cfg = config(kUnreachableUrl);
  cfg.max_pending = 2;
  common::audit::Client client(cfg);

  EXPECT_TRUE(client.emit(event("e0")));
  EXPECT_TRUE(client.emit(event("e1")));
  EXPECT_FALSE(client.emit(event("e2")));
  EXPECT_EQ(client.pending(), 2u);
  EXPECT_EQ(countFiles(spool_), 2u);
}

TEST_F(AuditClientTest, SecondProcessOnTheSameSpoolFallsBackToMemory) {
  common::audit::Client first(config(kUnreachableUrl));
  common::audit::Client second(config(kUnreachableUrl));
  EXPECT_TRUE(first.durable());
  EXPECT_FALSE(second.durable());
  EXPECT_TRUE(second.enabled());
}

TEST_F(AuditClientTest, MemoryModeStillDeliversAsynchronously) {
  FakeAuditServer server([](const json&) { return 201; });
  common::audit::Client client(config(server.url(), /*durable=*/false));
  ASSERT_TRUE(client.enabled());
  EXPECT_FALSE(client.durable());

  ASSERT_TRUE(client.emit(event("conversation_created")));
  ASSERT_TRUE(client.flush(5s));
  EXPECT_EQ(server.stored().size(), 1u);
}

TEST_F(AuditClientTest, RefusesInvalidEvents) {
  FakeAuditServer server([](const json&) { return 201; });
  common::audit::Client client(config(server.url()));

  auto invalid = event("x");
  invalid.outcome = "maybe";
  EXPECT_FALSE(client.emit(invalid));
  EXPECT_FALSE(client.emit(event("")));
  EXPECT_EQ(client.pending(), 0u);
}

TEST_F(AuditClientTest, DisabledClientIsANoOp) {
  auto cfg = config(kUnreachableUrl);
  cfg.enabled = false;
  common::audit::Client client(cfg);
  EXPECT_FALSE(client.enabled());
  EXPECT_FALSE(client.emit(event("e")));
  EXPECT_TRUE(client.flush(10ms));
}

}  // namespace
