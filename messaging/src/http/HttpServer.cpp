#include "messaging/http/HttpServer.hpp"

#include <boost/beast/core.hpp>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace messaging::http {

namespace asio = boost::asio;
namespace beast = boost::beast;
using tcp = asio::ip::tcp;

namespace {

Response errorResponse(beast_http::status status, const char* code,
                       unsigned version) {
  Response response{status, version};
  response.set(beast_http::field::server, "SecureChat-Messaging");
  response.set(beast_http::field::content_type, "application/json");
  response.body() = std::string(R"({"error":")") + code + "\"}";
  return response;
}

// One connection: async read -> handler on a worker -> async write -> close.
// The stream lives on its own strand; the handler result is posted back to
// it before writing, so the socket is only ever touched from that strand.
class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(tcp::socket socket, const Handler& handler,
          asio::thread_pool& workers, std::size_t bodyLimit)
      : stream_(std::move(socket)), handler_(handler), workers_(workers) {
    parser_.emplace();
    parser_->body_limit(bodyLimit);
  }

  void start() {
    stream_.expires_after(kIoTimeout);
    beast_http::async_read(
        stream_, buffer_, *parser_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
          self->onRead(ec);
        });
  }

 private:
  void onRead(beast::error_code ec) {
    if (ec == beast_http::error::body_limit) {
      const unsigned version =
          parser_->is_header_done() ? parser_->get().version() : 11;
      write(errorResponse(beast_http::status::payload_too_large,
                          "MESSAGE_TOO_LARGE", version));
      return;
    }
    if (ec) {
      return;  // timeout, client gone or malformed request: just drop it
    }

    // No I/O is pending until the handler is done, so stop the timer.
    stream_.expires_never();
    asio::post(workers_, [self = shared_from_this()] { self->handle(); });
  }

  // Runs on a worker thread.
  void handle() {
    const Request& request = parser_->get();
    Response response;
    try {
      response = handler_(request);
    } catch (const std::exception& e) {
      std::cerr << "[messaging] handler error: " << e.what() << '\n';
      response = errorResponse(beast_http::status::internal_server_error,
                               "INTERNAL_ERROR", request.version());
    } catch (...) {
      std::cerr << "[messaging] handler error: unknown exception\n";
      response = errorResponse(beast_http::status::internal_server_error,
                               "INTERNAL_ERROR", request.version());
    }

    asio::post(stream_.get_executor(),
               [self = shared_from_this(),
                response = std::move(response)]() mutable {
                 self->write(std::move(response));
               });
  }

  void write(Response response) {
    response_ = std::move(response);
    response_.keep_alive(false);
    response_.prepare_payload();

    stream_.expires_after(kIoTimeout);
    beast_http::async_write(
        stream_, response_,
        [self = shared_from_this()](beast::error_code, std::size_t) {
          beast::error_code ignored;
          self->stream_.socket().shutdown(tcp::socket::shutdown_send, ignored);
        });
  }

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::optional<beast_http::request_parser<beast_http::string_body>> parser_;
  Response response_;
  const Handler& handler_;
  asio::thread_pool& workers_;
};

}  // namespace

HttpServer::HttpServer(asio::io_context& ioContext, std::uint16_t port,
                       Handler handler, std::size_t workerThreads,
                       std::size_t bodyLimit)
    : ioContext_(ioContext),
      acceptor_(ioContext),
      port_(port),
      handler_(std::move(handler)),
      bodyLimit_(bodyLimit),
      workers_(workerThreads == 0 ? 1 : workerThreads) {}

HttpServer::~HttpServer() {
  beast::error_code ignored;
  acceptor_.close(ignored);
  workers_.stop();
  workers_.join();
}

void HttpServer::start() {
  const tcp::endpoint endpoint(tcp::v4(), port_);
  acceptor_.open(endpoint.protocol());
  acceptor_.set_option(tcp::acceptor::reuse_address(true));
  acceptor_.bind(endpoint);
  acceptor_.listen();
  acceptConnection();
}

std::uint16_t HttpServer::localPort() const {
  return acceptor_.local_endpoint().port();
}

void HttpServer::acceptConnection() {
  // Each connection gets its own strand so its handlers never run
  // concurrently, even if the io_context is run from several threads.
  acceptor_.async_accept(
      asio::make_strand(ioContext_),
      [this](const boost::system::error_code& ec, tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket), handler_, workers_,
                                    bodyLimit_)
              ->start();
        } else if (ec == asio::error::operation_aborted) {
          return;  // acceptor closed
        } else {
          std::cerr << "[messaging] accept error: " << ec.message() << '\n';
        }

        if (acceptor_.is_open()) {
          acceptConnection();
        }
      });
}

}  // namespace messaging::http
