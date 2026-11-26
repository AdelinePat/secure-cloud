#include "gateway/HttpServer.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "gateway/HttpResponse.hpp"
#include "gateway/HttpRouter.hpp"

namespace gateway {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

HttpServer::HttpServer(boost::asio::io_context& ioContext, std::uint16_t port)
    : ioContext_(ioContext),
      port_(port),
      acceptor_(ioContext),
      httpClient_(),
      router_(httpClient_) {}

void HttpServer::start() {
  using boost::asio::ip::tcp;

  // Listen on all IPv4 network interfaces
  tcp::endpoint endpoint(tcp::v4(), port_);

  // Open the TCP acceptor
  acceptor_.open(endpoint.protocol());

  // Allow the address to be reused after restarting the server
  acceptor_.set_option(tcp::acceptor::reuse_address(true));

  // Bind the acceptor to the configured port
  acceptor_.bind(endpoint);

  // Start listening for incoming connections
  acceptor_.listen();

  // Start accepting incoming TCP connections
  acceptConnection();
}

class HttpSession : public std::enable_shared_from_this<HttpSession> {
 public:
  explicit HttpSession(tcp::socket socket, HttpRouter& router)
      : stream_(std::move(socket)), router_(router) {}

  void start() { readRequest(); }

 private:
  void readRequest() {
    auto self = shared_from_this();

    // The body limit depends on the target (file chunk uploads may be larger
    // than other requests), so read the headers first. Beast compares
    // Content-Length with the limit while parsing the headers, so they are
    // read with the largest limit and the per-route one is checked after.
    parser_.emplace();
    parser_->body_limit(kFileBodyLimit);

    http::async_read_header(
        stream_, buffer_, *parser_, [self](beast::error_code ec, std::size_t) {
          self->onHeader(ec);
        });
  }

  void onHeader(beast::error_code ec) {
    if (ec == http::error::body_limit) {
      rejectTooLarge();
      return;
    }
    if (ec) {
      return;
    }

    const auto target = parser_->get().target();
    const std::size_t limit = HttpRouter::bodyLimitFor(
        std::string_view(target.data(), target.size()));

    const auto contentLength = parser_->content_length();
    if (contentLength && *contentLength > limit) {
      rejectTooLarge();
      return;
    }
    parser_->body_limit(limit);  // still enforced for chunked bodies

    auto self = shared_from_this();
    http::async_read(stream_, buffer_, *parser_,
                     [self](beast::error_code ec, std::size_t) {
                       self->onRead(ec);
                     });
  }

  void onRead(beast::error_code ec) {
    if (ec == http::error::body_limit) {
      rejectTooLarge();
      return;
    }
    if (ec) {
      return;
    }

    sendResponse(router_.route(parser_->get()));
  }

  void rejectTooLarge() {
    const unsigned version =
        parser_->is_header_done() ? parser_->get().version() : 11;
    http::response<http::string_body> response{http::status::payload_too_large,
                                               version};
    response.set(http::field::content_type, "application/json");
    response.body() = R"({"error":"PAYLOAD_TOO_LARGE"})";
    response.prepare_payload();
    // The client is usually still sending the body: read and discard it
    // after answering, otherwise closing with unread data resets the
    // connection and the client never sees the 413.
    sendResponse(std::move(response), /*drainRequest=*/true);
  }

  void sendResponse(http::response<http::string_body> response,
                    bool drainRequest = false) {
    // Keep the response alive until the asynchronous write completes
    response_ = std::move(response);
    response_.keep_alive(false);

    auto self = shared_from_this();

    http::async_write(stream_, response_,
                      [self, drainRequest](beast::error_code ec, std::size_t) {
                        if (ec) {
                          return;
                        }

                        beast::error_code shutdownError;
                        self->stream_.socket().shutdown(
                            tcp::socket::shutdown_send, shutdownError);

                        if (drainRequest) {
                          self->stream_.expires_after(std::chrono::seconds(5));
                          self->drain();
                        }
                      });
  }

  // Discards incoming bytes until the client closes or the timeout expires.
  void drain() {
    auto self = shared_from_this();
    stream_.async_read_some(
        boost::asio::buffer(drainBuffer_),
        [self](beast::error_code ec, std::size_t) {
          if (!ec) self->drain();
        });
  }

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;

  std::optional<http::request_parser<http::string_body>> parser_;
  http::response<http::string_body> response_;
  std::array<char, 8192> drainBuffer_{};
  HttpRouter& router_;
};

std::uint16_t HttpServer::localPort() const {
  // Return the actual port bound to the TCP acceptor
  return acceptor_.local_endpoint().port();
}

void HttpServer::acceptConnection() {
  using boost::asio::ip::tcp;

  acceptor_.async_accept(
      [this](const boost::system::error_code& ec, tcp::socket socket) {
        if (!ec) {
          ++acceptedConnections_;

          std::make_shared<HttpSession>(std::move(socket), router_)->start();
        } else {
          std::cerr << "[SERVER] Accept error: " << ec.message() << '\n';
        }

        if (acceptor_.is_open()) {
          acceptConnection();
        }
      });
}

std::size_t HttpServer::acceptedConnections() const {
  return acceptedConnections_;
}

}  // namespace gateway