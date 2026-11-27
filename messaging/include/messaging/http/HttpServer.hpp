#pragma once

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/beast/http.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace messaging::http {

namespace beast_http = boost::beast::http;

using Request = beast_http::request<beast_http::string_body>;
using Response = beast_http::response<beast_http::string_body>;

// Called once per request, on a worker thread. Several calls run at the same
// time, so the handler must be thread-safe. Exceptions become a 500.
using Handler = std::function<Response(const Request&)>;

// Largest request body accepted. A message carries at most 1 MiB of
// ciphertext, base64-encoded in JSON.
inline constexpr std::size_t kMaxRequestBodyBytes = 2 * 1024 * 1024;

// A connection that has not sent its full request (or read the response)
// within this time is dropped, so stalled clients cannot pin sessions.
inline constexpr std::chrono::seconds kIoTimeout{30};

// HTTP/1.1 server, one request per connection (the gateway opens a new
// connection per proxied request).
//
// Network I/O is asynchronous on the io_context, so a slow client never
// blocks anyone. Handlers run on a separate pool of `workerThreads` threads,
// so a slow database call only occupies one worker while the others keep
// serving requests.
class HttpServer {
 public:
  HttpServer(boost::asio::io_context& ioContext, std::uint16_t port,
             Handler handler, std::size_t workerThreads,
             std::size_t bodyLimit = kMaxRequestBodyBytes);

  // Stops accepting and waits for running handlers to finish. Handlers that
  // have not started yet are dropped.
  ~HttpServer();

  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  void start();
  std::uint16_t localPort() const;

 private:
  void acceptConnection();

  boost::asio::io_context& ioContext_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::uint16_t port_;
  Handler handler_;
  std::size_t bodyLimit_;
  boost::asio::thread_pool workers_;
};

}  // namespace messaging::http
