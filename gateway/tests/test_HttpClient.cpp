#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <exception>
#include <future>
#include <string>
#include <thread>

#include "gateway/HttpClient.hpp"

TEST(HttpClientTests, SendsRequestAndReceivesResponse) {
  namespace net = boost::asio;
  namespace beast = boost::beast;
  namespace http = beast::http;

  using tcp = net::ip::tcp;

  net::io_context serverContext;

  // Use port 0 to let the operating system select an available port
  tcp::acceptor acceptor(serverContext,
                         tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));

  const auto port = acceptor.local_endpoint().port();

  std::promise<std::string> receivedTargetPromise;
  auto receivedTarget = receivedTargetPromise.get_future();

  // Simulate an internal HTTP microservice
  std::jthread serverThread([&]() {
    try {
      tcp::socket socket(serverContext);
      acceptor.accept(socket);

      beast::flat_buffer buffer;
      http::request<http::string_body> incomingRequest;

      http::read(socket, buffer, incomingRequest);

      receivedTargetPromise.set_value(std::string(incomingRequest.target()));

      http::response<http::string_body> response{http::status::ok, 11};

      response.set(http::field::content_type, "application/json");
      response.body() = R"({"service":"mock-auth","status":"ok"})";
      response.prepare_payload();

      http::write(socket, response);

      beast::error_code ec;
      socket.shutdown(tcp::socket::shutdown_send, ec);

    } catch (...) {
      receivedTargetPromise.set_exception(std::current_exception());
    }
  });

  gateway::HttpClient client;

  http::request<http::string_body> request{http::verb::get, "/auth/test", 11};

  request.set(http::field::host, "127.0.0.1");

  // Send the request to our local mock service
  auto response = client.send("127.0.0.1", port, request);

  EXPECT_EQ(response.result(), http::status::ok);

  EXPECT_EQ(response.body(), R"({"service":"mock-auth","status":"ok"})");

  // Verify that the mock service received the expected route
  EXPECT_EQ(receivedTarget.get(), "/auth/test");
}