#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <cstdlib>
#include <optional>
#include <string>
#include <thread>

#include "gateway/HttpRouter.hpp"
#include "gateway/HttpServer.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

namespace {

// Stands in for the file service: accepts one connection, records the
// request it receives and answers with a fixed response.
class FakeFileService {
 public:
  FakeFileService() : acceptor_(context_, {boost::asio::ip::make_address("127.0.0.1"), 0}) {
    thread_ = std::thread([this] {
      tcp::socket socket(context_);
      boost::system::error_code ec;
      acceptor_.accept(socket, ec);
      if (ec) return;

      beast::flat_buffer buffer;
      http::request_parser<http::string_body> parser;
      parser.body_limit(8 * 1024 * 1024);
      http::read(socket, buffer, parser, ec);
      if (ec) return;
      received_ = parser.release();

      http::response<http::string_body> response{http::status::created, 11};
      response.set(http::field::content_type, "application/json");
      response.body() = R"({"file_id":"f"})";
      response.prepare_payload();
      http::write(socket, response, ec);
      socket.shutdown(tcp::socket::shutdown_both, ec);
    });
  }

  ~FakeFileService() {
    acceptor_.close();
    if (thread_.joinable()) thread_.join();
  }

  std::uint16_t port() const { return acceptor_.local_endpoint().port(); }

  // Valid once the router call has returned.
  const std::optional<http::request<http::string_body>>& received() {
    if (thread_.joinable()) thread_.join();
    return received_;
  }

 private:
  boost::asio::io_context context_;
  tcp::acceptor acceptor_;
  std::thread thread_;
  std::optional<http::request<http::string_body>> received_;
};

// Points the gateway at a port and restores the environment afterwards.
class FileEnv {
 public:
  explicit FileEnv(std::uint16_t port) {
    setenv("FILE_HOST", "127.0.0.1", 1);
    setenv("FILE_PORT", std::to_string(port).c_str(), 1);
  }
  ~FileEnv() {
    unsetenv("FILE_HOST");
    unsetenv("FILE_PORT");
  }
};

// A local port with nothing listening on it.
std::uint16_t closedPort() {
  boost::asio::io_context context;
  tcp::acceptor acceptor(context, {boost::asio::ip::make_address("127.0.0.1"), 0});
  return acceptor.local_endpoint().port();
}

}  // namespace

// ── route matching ─────────────────────────────────────────────────────────

TEST(FileRoutes, MatchesOnlyFilesPaths) {
  EXPECT_TRUE(gateway::HttpRouter::isFileRoute("/files"));
  EXPECT_TRUE(gateway::HttpRouter::isFileRoute("/files/abc/chunks/0"));
  EXPECT_TRUE(gateway::HttpRouter::isFileRoute("/files?x=1"));
  EXPECT_FALSE(gateway::HttpRouter::isFileRoute("/filesystem"));
  EXPECT_FALSE(gateway::HttpRouter::isFileRoute("/file"));
  EXPECT_FALSE(gateway::HttpRouter::isFileRoute("/messaging/files"));
}

TEST(FileRoutes, OnlyFileRoutesGetTheLargerBodyLimit) {
  EXPECT_EQ(gateway::HttpRouter::bodyLimitFor("/files/a/chunks/0"),
            gateway::kFileBodyLimit);
  EXPECT_EQ(gateway::HttpRouter::bodyLimitFor("/auth/login"),
            gateway::kDefaultBodyLimit);
  EXPECT_EQ(gateway::HttpRouter::bodyLimitFor("/messaging/conversations"),
            gateway::kDefaultBodyLimit);
}

// ── proxying ───────────────────────────────────────────────────────────────

TEST(FileRoutes, ForwardsRequestUnchangedExceptCookie) {
  FakeFileService upstream;
  FileEnv env(upstream.port());

  gateway::HttpClient client;
  gateway::HttpRouter router(client);

  const std::string chunk(1024 * 1024 + 17, '\x7f');
  http::request<http::string_body> request{
      http::verb::put, "/files/0a1b2c3d-0000-4000-8000-000000000001/chunks/3",
      11};
  request.set(http::field::authorization, "Bearer token-123");
  request.set(http::field::content_type, "application/octet-stream");
  request.set(http::field::cookie, "session=secret");
  request.body() = chunk;
  request.prepare_payload();

  const auto response = router.route(request);

  // The file service's answer comes back as-is.
  EXPECT_EQ(response.result(), http::status::created);
  EXPECT_EQ(response.body(), R"({"file_id":"f"})");

  const auto& forwarded = upstream.received();
  ASSERT_TRUE(forwarded.has_value());
  EXPECT_EQ(forwarded->method(), http::verb::put);
  EXPECT_EQ(forwarded->target(),
            "/files/0a1b2c3d-0000-4000-8000-000000000001/chunks/3");
  EXPECT_EQ((*forwarded)[http::field::authorization], "Bearer token-123");
  EXPECT_EQ((*forwarded)[http::field::content_type],
            "application/octet-stream");
  EXPECT_EQ(forwarded->count(http::field::cookie), 0u);
  EXPECT_EQ(forwarded->body(), chunk);
}

TEST(FileRoutes, ForwardsEveryMethodToTheFileService) {
  // Method checks are the file service's job (it answers 405 itself).
  for (const auto method : {http::verb::get, http::verb::post,
                            http::verb::delete_, http::verb::patch}) {
    FakeFileService upstream;
    FileEnv env(upstream.port());
    gateway::HttpClient client;
    gateway::HttpRouter router(client);

    http::request<http::string_body> request{method, "/files", 11};
    router.route(request);

    ASSERT_TRUE(upstream.received().has_value());
    EXPECT_EQ(upstream.received()->method(), method);
  }
}

TEST(FileRoutes, UnreachableFileServiceIsBadGateway) {
  FileEnv env(closedPort());
  gateway::HttpClient client;
  gateway::HttpRouter router(client);

  http::request<http::string_body> request{http::verb::get, "/files/x", 11};
  const auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::bad_gateway);
  EXPECT_EQ(response.body(), R"({"error":"FILE_SERVICE_UNAVAILABLE"})");
}

// ── body limits through the real server ────────────────────────────────────

namespace {

http::response<http::string_body> sendThroughGateway(const std::string& target,
                                                     std::size_t bodySize) {
  boost::asio::io_context serverContext;
  gateway::HttpServer server(serverContext, 0);
  server.start();
  std::thread serverThread([&serverContext] { serverContext.run(); });

  http::response<http::string_body> response;
  try {
    boost::asio::io_context clientContext;
    beast::tcp_stream stream(clientContext);
    stream.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                                 server.localPort()));

    http::request<http::string_body> request{http::verb::put, target, 11};
    request.set(http::field::host, "localhost");
    request.body() = std::string(bodySize, 'x');
    request.prepare_payload();
    http::write(stream, request);

    beast::flat_buffer buffer;
    http::read(stream, buffer, response);
  } catch (const std::exception& e) {
    ADD_FAILURE() << "client error: " << e.what();
  }

  serverContext.stop();
  serverThread.join();
  return response;
}

}  // namespace

TEST(FileRoutes, ServerAcceptsChunkSizedBodiesOnFileRoutes) {
  // Nothing listens on the file port, so getting past the body limit shows
  // up as 502 from the proxy instead of 413.
  FileEnv env(closedPort());
  const auto response = sendThroughGateway("/files/x/chunks/0", 1024 * 1024 + 17);
  EXPECT_EQ(response.result(), http::status::bad_gateway);
}

TEST(FileRoutes, ServerRejectsBodiesOverTheFileLimit) {
  FileEnv env(closedPort());
  const auto response =
      sendThroughGateway("/files/x/chunks/0", gateway::kFileBodyLimit + 1);
  EXPECT_EQ(response.result(), http::status::payload_too_large);
  EXPECT_EQ(response.body(), R"({"error":"PAYLOAD_TOO_LARGE"})");
}

TEST(FileRoutes, OtherRoutesKeepTheDefaultLimit) {
  const auto response =
      sendThroughGateway("/auth/login", gateway::kDefaultBodyLimit + 1);
  EXPECT_EQ(response.result(), http::status::payload_too_large);
}
