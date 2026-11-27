#include "gateway/HttpServer.hpp"

#include <openssl/ssl.h>

#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gateway/HttpResponse.hpp"
#include "gateway/HttpRouter.hpp"

namespace gateway {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

namespace ssl = boost::asio::ssl;

namespace {

// A client that connects but never finishes the TLS handshake, or never
// sends a complete request, is dropped after these delays.
constexpr std::chrono::seconds kHandshakeTimeout{30};
constexpr std::chrono::seconds kReadTimeout{60};
constexpr std::chrono::seconds kShutdownTimeout{5};

std::unique_ptr<ssl::context> makeSslContext(const TlsFiles& files) {
  auto context = std::make_unique<ssl::context>(ssl::context::tls_server);
  context->set_options(ssl::context::default_workarounds |
                       ssl::context::no_sslv2 | ssl::context::no_sslv3 |
                       ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1 |
                       ssl::context::single_dh_use);
  context->use_certificate_chain_file(files.certificateChain);
  context->use_private_key_file(files.privateKey, ssl::context::pem);
  if (SSL_CTX_check_private_key(context->native_handle()) != 1) {
    throw std::runtime_error("TLS private key does not match the certificate");
  }
  return context;
}

}  // namespace

HttpServer::HttpServer(boost::asio::io_context& ioContext, std::uint16_t port,
                       std::optional<TlsFiles> tls)
    : ioContext_(ioContext),
      port_(port),
      acceptor_(ioContext),
      sslContext_(tls ? makeSslContext(*tls) : nullptr),
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

// One connection. Stream is beast::tcp_stream (plain HTTP) or
// beast::ssl_stream<beast::tcp_stream> (HTTPS): the HTTP handling is the
// same, only the handshake and the shutdown differ.
template <class Stream>
class HttpSession : public std::enable_shared_from_this<HttpSession<Stream>> {
  static constexpr bool kTls =
      !std::is_same_v<Stream, beast::tcp_stream>;

 public:
  // Plain HTTP.
  HttpSession(tcp::socket socket, HttpRouter& router)
    requires(!kTls)
      : stream_(std::move(socket)), router_(router) {}

  // HTTPS.
  HttpSession(tcp::socket socket, ssl::context& context, HttpRouter& router)
    requires(kTls)
      : stream_(std::move(socket), context), router_(router) {}

  void start() {
    if constexpr (kTls) {
      auto self = this->shared_from_this();
      lowest().expires_after(kHandshakeTimeout);
      stream_.async_handshake(ssl::stream_base::server,
                              [self](beast::error_code ec) {
                                if (!ec) self->readRequest();
                              });
    } else {
      readRequest();
    }
  }

 private:
  beast::tcp_stream& lowest() { return beast::get_lowest_layer(stream_); }

  void readRequest() {
    auto self = this->shared_from_this();
    lowest().expires_after(kReadTimeout);

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

    auto self = this->shared_from_this();
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

    // The router calls the backend synchronously; don't let the read
    // timeout fire meanwhile.
    lowest().expires_never();
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

    auto self = this->shared_from_this();

    lowest().expires_after(kReadTimeout);
    http::async_write(stream_, response_,
                      [self, drainRequest](beast::error_code ec, std::size_t) {
                        if (ec) {
                          return;
                        }

                        if (drainRequest) {
                          if constexpr (!kTls) {
                            // Plain TCP: signal the end of the response
                            // first, then discard what is still coming.
                            beast::error_code ignored;
                            self->lowest().socket().shutdown(
                                tcp::socket::shutdown_send, ignored);
                          }
                          self->lowest().expires_after(std::chrono::seconds(5));
                          self->drain();
                          return;
                        }
                        self->close();
                      });
  }

  // Discards incoming bytes until the client closes or the timeout expires.
  void drain() {
    auto self = this->shared_from_this();
    stream_.async_read_some(
        boost::asio::buffer(drainBuffer_),
        [self](beast::error_code ec, std::size_t) {
          if (!ec) {
            self->drain();
          } else if constexpr (kTls) {
            self->close();
          }
        });
  }

  // Ends the connection once the response is written. TLS sends a
  // close_notify so the client can tell a complete response from a cut one.
  void close() {
    if constexpr (kTls) {
      auto self = this->shared_from_this();
      lowest().expires_after(kShutdownTimeout);
      stream_.async_shutdown([self](beast::error_code) {
        self->lowest().close();
      });
    } else {
      beast::error_code ignored;
      lowest().socket().shutdown(tcp::socket::shutdown_send, ignored);
    }
  }

  Stream stream_;
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


          if (sslContext_) {
            std::make_shared<HttpSession<beast::ssl_stream<beast::tcp_stream>>>(
                std::move(socket), *sslContext_, router_)
                ->start();
          } else {
            std::make_shared<HttpSession<beast::tcp_stream>>(std::move(socket),
                                                             router_)
                ->start();
          }
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