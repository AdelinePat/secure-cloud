#include "file/http/HttpServer.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace beast = boost::beast;
namespace beast_http = beast::http;
using tcp = boost::asio::ip::tcp;
using file::http::HttpServer;
using file::http::Request;
using file::http::Response;

namespace {

// Echoes the received body size so tests can see what reached the handler.
Response echoSize(const Request& request) {
  Response response{beast_http::status::ok, request.version()};
  response.body() = std::to_string(request.body().size());
  return response;
}

class HttpServerTest : public ::testing::Test {
 protected:
  void startServer(file::http::Handler handler, std::size_t bodyLimit) {
    server_.emplace(serverContext_, 0, std::move(handler), bodyLimit);
    server_->start();
    serverThread_ = std::thread([this] { serverContext_.run(); });
  }

  void TearDown() override {
    serverContext_.stop();
    if (serverThread_.joinable()) serverThread_.join();
  }

  beast_http::response<beast_http::string_body> send(
      beast_http::verb method, const std::string& target,
      const std::string& body) {
    boost::asio::io_context clientContext;
    beast::tcp_stream stream(clientContext);
    stream.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                                 server_->localPort()));

    Request request{method, target, 11};
    request.set(beast_http::field::host, "localhost");
    request.body() = body;
    request.prepare_payload();
    beast_http::write(stream, request);

    beast::flat_buffer buffer;
    beast_http::response<beast_http::string_body> response;
    beast_http::read(stream, buffer, response);
    return response;
  }

  boost::asio::io_context serverContext_;
  std::optional<HttpServer> server_;
  std::thread serverThread_;
};

}  // namespace

TEST_F(HttpServerTest, DispatchesRequestToHandler) {
  startServer(echoSize, 1024);
  auto response = send(beast_http::verb::put, "/x", std::string(100, 'a'));
  EXPECT_EQ(response.result(), beast_http::status::ok);
  EXPECT_EQ(response.body(), "100");
}

TEST_F(HttpServerTest, AcceptsBodyAtTheLimit) {
  startServer(echoSize, 1024);
  auto response = send(beast_http::verb::put, "/x", std::string(1024, 'a'));
  EXPECT_EQ(response.result(), beast_http::status::ok);
  EXPECT_EQ(response.body(), "1024");
}

TEST_F(HttpServerTest, RejectsBodyOverTheLimitWith413) {
  startServer(echoSize, 1024);
  auto response = send(beast_http::verb::put, "/x", std::string(1025, 'a'));
  EXPECT_EQ(response.result(), beast_http::status::payload_too_large);
  EXPECT_EQ(response.body(), R"({"error":"PAYLOAD_TOO_LARGE"})");
}

TEST_F(HttpServerTest, DefaultLimitAcceptsAOneMebibyteChunk) {
  server_.emplace(serverContext_, 0, echoSize);
  server_->start();
  serverThread_ = std::thread([this] { serverContext_.run(); });

  const std::size_t chunk = 1024 * 1024 + 17;  // 1 MiB + secretstream overhead
  auto response = send(beast_http::verb::put, "/x", std::string(chunk, 'a'));
  EXPECT_EQ(response.result(), beast_http::status::ok);
  EXPECT_EQ(response.body(), std::to_string(chunk));
}

TEST_F(HttpServerTest, HandlerExceptionBecomes500) {
  startServer([](const Request&) -> Response { throw std::runtime_error("x"); },
              1024);
  auto response = send(beast_http::verb::get, "/x", "");
  EXPECT_EQ(response.result(), beast_http::status::internal_server_error);
  EXPECT_EQ(response.body(), R"({"error":"INTERNAL_ERROR"})");
}

TEST_F(HttpServerTest, ServesConsecutiveConnections) {
  startServer(echoSize, 1024);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(send(beast_http::verb::put, "/x", "abc").body(), "3");
  }
}
