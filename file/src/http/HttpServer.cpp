#include "file/http/HttpServer.hpp"

#include <boost/beast/core.hpp>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

namespace file::http {

namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

namespace {

Response errorResponse(beast_http::status status, const char* code,
                       unsigned version) {
  Response response{status, version};
  response.set(beast_http::field::server, "SecureChat-File");
  response.set(beast_http::field::content_type, "application/json");
  response.body() = std::string(R"({"error":")") + code + "\"}";
  return response;
}

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(tcp::socket socket, const Handler& handler, std::size_t bodyLimit)
      : stream_(std::move(socket)), handler_(handler) {
    parser_.emplace();
    parser_->body_limit(bodyLimit);
  }

  void start() {
    stream_.expires_after(kReadTimeout);
    beast_http::async_read(
        stream_, buffer_, *parser_,
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
          self->onRead(ec);
        });
  }

 private:
  void onRead(beast::error_code ec) {
    if (ec == beast_http::error::body_limit) {
      write(errorResponse(beast_http::status::payload_too_large,
                          "PAYLOAD_TOO_LARGE", 11));
      return;
    }
    if (ec) {
      return;  // timeout, client gone or malformed request: just drop it
    }

    const Request& request = parser_->get();
    Response response;
    try {
      response = handler_(request);
    } catch (const std::exception& e) {
      std::cerr << "[file] handler error: " << e.what() << '\n';
      response = errorResponse(beast_http::status::internal_server_error,
                               "INTERNAL_ERROR", request.version());
    }
    write(std::move(response));
  }

  void write(Response response) {
    response_ = std::move(response);
    response_.keep_alive(false);
    response_.prepare_payload();

    stream_.expires_after(kReadTimeout);
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
};

}  // namespace

HttpServer::HttpServer(boost::asio::io_context& ioContext, std::uint16_t port,
                       Handler handler, std::size_t bodyLimit)
    : acceptor_(ioContext),
      port_(port),
      handler_(std::move(handler)),
      bodyLimit_(bodyLimit) {}

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
  acceptor_.async_accept(
      [this](const boost::system::error_code& ec, tcp::socket socket) {
        if (!ec) {
          std::make_shared<Session>(std::move(socket), handler_, bodyLimit_)
              ->start();
        } else if (ec != boost::asio::error::operation_aborted) {
          std::cerr << "[file] accept error: " << ec.message() << '\n';
        }

        if (acceptor_.is_open()) {
          acceptConnection();
        }
      });
}

}  // namespace file::http
