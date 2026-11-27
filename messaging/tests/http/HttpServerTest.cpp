#include "messaging/http/HttpServer.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <future>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace beast_http = beast::http;
using tcp = boost::asio::ip::tcp;
using messaging::http::HttpServer;
using messaging::http::Request;
using messaging::http::Response;

namespace {

using ClientResponse = beast_http::response<beast_http::string_body>;

class HttpServerTest : public ::testing::Test {
 protected:
  void startServer(messaging::http::Handler handler, std::size_t workers,
                   std::size_t bodyLimit = messaging::http::kMaxRequestBodyBytes) {
    server_.emplace(serverContext_, 0, std::move(handler), workers, bodyLimit);
    server_->start();
    serverThread_ = std::thread([this] { serverContext_.run(); });
  }

  void TearDown() override {
    serverContext_.stop();
    if (serverThread_.joinable()) serverThread_.join();
    server_.reset();
  }

  ClientResponse send(const std::string& target, const std::string& body = "") {
    boost::asio::io_context clientContext;
    beast::tcp_stream stream(clientContext);
    stream.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                                 server_->localPort()));

    Request request{beast_http::verb::post, target, 11};
    request.set(beast_http::field::host, "localhost");
    request.body() = body;
    request.prepare_payload();
    beast_http::write(stream, request);

    beast::flat_buffer buffer;
    ClientResponse response;
    beast_http::read(stream, buffer, response);
    return response;
  }

  boost::asio::io_context serverContext_;
  std::optional<HttpServer> server_;
  std::thread serverThread_;
};

Response ok(const Request& request, std::string body) {
  Response response{beast_http::status::ok, request.version()};
  response.body() = std::move(body);
  return response;
}

TEST_F(HttpServerTest, PassesRequestToHandler) {
  startServer([](const Request& r) { return ok(r, "got " + r.body()); }, 2);

  auto response = send("/x", "hello");
  EXPECT_EQ(response.result(), beast_http::status::ok);
  EXPECT_EQ(response.body(), "got hello");
}

// The point of this server: a slow handler (e.g. a slow database query)
// must not stop other requests from being served.
TEST_F(HttpServerTest, SlowRequestDoesNotBlockOthers) {
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  std::promise<void> slowStarted;

  startServer(
      [&](const Request& r) {
        if (r.target() == "/slow") {
          slowStarted.set_value();
          released.wait();
          return ok(r, "slow");
        }
        return ok(r, "fast");
      },
      2);

  auto slow = std::async(std::launch::async, [this] { return send("/slow"); });
  slowStarted.get_future().wait();

  // Served while /slow is still running.
  EXPECT_EQ(send("/fast").body(), "fast");
  EXPECT_EQ(slow.wait_for(std::chrono::milliseconds(0)),
            std::future_status::timeout);

  release.set_value();
  EXPECT_EQ(slow.get().body(), "slow");
}

TEST_F(HttpServerTest, HandlerExceptionBecomesInternalError) {
  startServer([](const Request&) -> Response { throw std::runtime_error("boom"); },
              1);

  auto response = send("/x");
  EXPECT_EQ(response.result(), beast_http::status::internal_server_error);
  EXPECT_EQ(response.body(), R"({"error":"INTERNAL_ERROR"})");
}

TEST_F(HttpServerTest, OversizedBodyIsRejected) {
  bool called = false;
  startServer(
      [&](const Request& r) {
        called = true;
        return ok(r, "");
      },
      1, 16);

  auto response = send("/x", std::string(17, 'a'));
  EXPECT_EQ(response.result(), beast_http::status::payload_too_large);
  EXPECT_EQ(response.body(), R"({"error":"MESSAGE_TOO_LARGE"})");
  EXPECT_FALSE(called);
}

}  // namespace
