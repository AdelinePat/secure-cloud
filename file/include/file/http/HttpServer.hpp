#pragma once

#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace file::http {

namespace beast_http = boost::beast::http;

using Request = beast_http::request<beast_http::string_body>;
using Response = beast_http::response<beast_http::string_body>;
using Handler = std::function<Response(const Request&)>;

// Largest request body accepted. Clients upload ciphertext chunks of at most
// 1 MiB (+ secretstream overhead), so 2 MiB leaves headroom while keeping
// every request small enough to hold in memory.
inline constexpr std::size_t kMaxRequestBodyBytes = 2 * 1024 * 1024;

// A connection that has not sent its full request within this time is
// dropped, so stalled clients cannot pin sessions forever.
inline constexpr std::chrono::seconds kReadTimeout{60};

// Asynchronous HTTP/1.1 server, one request per connection (the gateway opens
// a new connection per proxied request). Reads are asynchronous, so a slow
// upload never blocks other clients; the handler itself runs on the
// io_context thread and must stay short.
class HttpServer {
 public:
  HttpServer(boost::asio::io_context& ioContext, std::uint16_t port,
             Handler handler, std::size_t bodyLimit = kMaxRequestBodyBytes);

  void start();
  std::uint16_t localPort() const;

 private:
  void acceptConnection();

  boost::asio::ip::tcp::acceptor acceptor_;
  std::uint16_t port_;
  Handler handler_;
  std::size_t bodyLimit_;
};

}  // namespace file::http
