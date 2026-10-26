#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "gateway/HttpServer.hpp"

TEST(HttpServerTests, AcceptsTcpConnection) {
  using boost::asio::ip::tcp;

  boost::asio::io_context serverContext;

  // Start the server on an available local TCP port
  gateway::HttpServer server(serverContext, 0);
  server.start();

  // Retrieve the actual port assigned by the operating system
  const auto port = server.localPort();

  EXPECT_NE(port, 0);

  // Create a local TCP client
  boost::asio::io_context clientContext;
  tcp::socket client(clientContext);

  const tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"),
                               port);

  boost::system::error_code ec;

  // Attempt to connect to the listening server
  client.connect(endpoint, ec);

  EXPECT_FALSE(ec) << ec.message();
  EXPECT_TRUE(client.is_open());
}

TEST(HttpServerTests, ProcessesIncomingConnection) {
  using boost::asio::ip::tcp;

  boost::asio::io_context serverContext;

  gateway::HttpServer server(serverContext, 0);
  server.start();

  // Connect a local TCP client
  boost::asio::io_context clientContext;
  tcp::socket client(clientContext);

  tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"),
                         server.localPort());

  client.connect(endpoint);

  // Process the pending asynchronous accept operation
  serverContext.poll();

  // Verify that the accept callback was executed
  EXPECT_EQ(server.acceptedConnections(), 1);
}

TEST(HttpServerTests, ReturnsHttpResponse) {
  namespace beast = boost::beast;
  namespace http = beast::http;
  using tcp = boost::asio::ip::tcp;

  boost::asio::io_context serverContext;

  gateway::HttpServer server(serverContext, 0);
  server.start();

  // Run the asynchronous server in a separate thread
  std::thread serverThread([&serverContext]() { serverContext.run(); });

  // Create an HTTP client
  boost::asio::io_context clientContext;

  beast::tcp_stream stream(clientContext);

  stream.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                               server.localPort()));

  // Build an HTTP GET request
  http::request<http::empty_body> request{http::verb::get, "/health", 11};

  request.set(http::field::host, "localhost");

  // Send the request
  http::write(stream, request);

  // Read the server response
  beast::flat_buffer buffer;
  http::response<http::string_body> response;

  http::read(stream, buffer, response);

  EXPECT_EQ(response.result(), http::status::ok);

  EXPECT_EQ(response.body(), R"({"status":"ok","service":"gateway"})");

  // Stop the server event loop
  serverContext.stop();
  serverThread.join();
}

TEST(HttpServerTests, ReturnsCorrectHttpStatusForRoutes) {
  namespace beast = boost::beast;
  namespace http = beast::http;
  using tcp = boost::asio::ip::tcp;

  boost::asio::io_context serverContext;

  gateway::HttpServer server(serverContext, 0);
  server.start();

  // Ensure the server event loop stops before joining the thread
  struct ServerRunner {
    boost::asio::io_context& context;
    std::jthread thread;

    explicit ServerRunner(boost::asio::io_context& ctx)
        : context(ctx), thread([&ctx]() { ctx.run(); }) {}

    ~ServerRunner() {
      // Stop pending asynchronous operations before joining the thread
      context.stop();

      if (thread.joinable()) {
        thread.join();
      }
    }
  };

  ServerRunner runner(serverContext);

  struct TestCase {
    http::verb method;
    std::string target;
    http::status expectedStatus;
  };

  const std::vector<TestCase> testCases = {
      {http::verb::get, "/health", http::status::ok},
      {http::verb::get, "/unknown", http::status::not_found},
      {http::verb::post, "/health", http::status::method_not_allowed}};

  for (const auto& testCase : testCases) {
    std::cerr << "[DEBUG] Testing: " << testCase.target << '\n';

    boost::asio::io_context clientContext;
    beast::tcp_stream stream(clientContext);

    stream.expires_after(std::chrono::seconds(3));

    std::cerr << "[DEBUG] Connecting..." << '\n';

    stream.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
                                 server.localPort()));

    std::cerr << "[DEBUG] Connected" << '\n';

    http::request<http::empty_body> request{testCase.method, testCase.target,
                                            11};

    request.set(http::field::host, "localhost");

    std::cerr << "[DEBUG] Sending request..." << '\n';

    http::write(stream, request);

    std::cerr << "[DEBUG] Reading response..." << '\n';

    beast::flat_buffer buffer;
    http::response<http::string_body> response;

    http::read(stream, buffer, response);

    std::cerr << "[DEBUG] Received HTTP " << response.result_int() << '\n';

    EXPECT_EQ(response.result(), testCase.expectedStatus)
        << "Failed for route: " << testCase.target;
  }
}