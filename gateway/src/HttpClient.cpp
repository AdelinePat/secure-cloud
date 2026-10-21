#include "gateway/HttpClient.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <chrono>
#include <string>

namespace gateway {

http::response<http::string_body> HttpClient::send(
    const std::string& host, std::uint16_t port,
    const http::request<http::string_body>& request) {
  namespace beast = boost::beast;
  namespace net = boost::asio;

  using tcp = net::ip::tcp;

  // Create the I/O context
  net::io_context ioContext;

  // Resolve the service hostname
  tcp::resolver resolver(ioContext);

  const auto endpoints = resolver.resolve(host, std::to_string(port));

  // Establish a TCP connection
  beast::tcp_stream stream(ioContext);
  stream.expires_after(std::chrono::seconds(5));
  stream.connect(endpoints);

  // Send the HTTP request
  http::write(stream, request);

  // Read the HTTP response
  beast::flat_buffer buffer;
  http::response<http::string_body> response;

  http::read(stream, buffer, response);

  // Close the TCP connection
  beast::error_code ec;
  stream.socket().shutdown(tcp::socket::shutdown_both, ec);

  return response;
}

}  // namespace gateway