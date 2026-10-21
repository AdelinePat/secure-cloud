#include "gateway/HttpServer.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <iostream>
#include <memory>

#include "gateway/HttpResponse.hpp"
#include "gateway/HttpRouter.hpp"

namespace gateway {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

HttpServer::HttpServer(boost::asio::io_context& ioContext, std::uint16_t port)
    : ioContext_(ioContext), port_(port), acceptor_(ioContext) {}

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
  explicit HttpSession(tcp::socket socket) : stream_(std::move(socket)) {}

  void start() { readRequest(); }

 private:
  void readRequest() {
    auto self = shared_from_this();

    // Read an incoming HTTP request
    http::async_read(stream_, buffer_, request_,
                     [self](beast::error_code ec, std::size_t) {
                       if (ec) {
                         return;
                       }

                       self->sendResponse();
                     });
  }

  void sendResponse() {
    // Keep the response alive until the asynchronous write completes
    response_ = HttpRouter::route(request_);
    response_.keep_alive(false);

    auto self = shared_from_this();

    http::async_write(stream_, response_,
                      [self](beast::error_code ec, std::size_t) {
                        if (ec) {
                          return;
                        }

                        beast::error_code shutdownError;
                        self->stream_.socket().shutdown(
                            tcp::socket::shutdown_send, shutdownError);
                      });
  }

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;

  http::request<http::string_body> request_;
  http::response<http::string_body> response_;
};

std::uint16_t HttpServer::localPort() const {
  // Return the actual port bound to the TCP acceptor
  return acceptor_.local_endpoint().port();
}

void HttpServer::acceptConnection() {
    using boost::asio::ip::tcp;

    std::cerr << "[SERVER] Waiting for connection..." << '\n';

    acceptor_.async_accept(
        [this](const boost::system::error_code& ec, tcp::socket socket) {
            if (!ec) {
                ++acceptedConnections_;

                std::cerr << "[SERVER] Connection accepted: "
                          << acceptedConnections_ << '\n';

                std::make_shared<HttpSession>(std::move(socket))->start();
            } else {
                std::cerr << "[SERVER] Accept error: "
                          << ec.message() << '\n';
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