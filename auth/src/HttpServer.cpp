#include "auth/HttpServer.hpp"

#include <utility>

#include "auth/HttpSession.hpp"

namespace auth {

HttpServer::HttpServer(asio::io_context& ioContext, std::uint16_t port,
                       services::AuthService& authService,
                       common::audit::Client* audit)
    : acceptor_(ioContext, tcp::endpoint(tcp::v4(), port)),
      router_(authService, audit) {}

void HttpServer::run() {
  for (;;) {
    // Wait for an incoming TCP connection
    tcp::socket socket = acceptor_.accept();

    // Create a session and transfer ownership of the socket
    HttpSession session(std::move(socket), router_);

    // Process the HTTP request
    session.run();
  }
}

}  // namespace auth