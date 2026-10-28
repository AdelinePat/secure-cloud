#include "messaging/http/HttpServer.hpp"

#include <utility>

#include "messaging/http/HttpSession.hpp"

namespace messaging::http {

HttpServer::HttpServer(asio::io_context& ioContext, std::uint16_t port,
                       HttpRouter& router)
    : acceptor_(ioContext, tcp::endpoint(tcp::v4(), port)), router_(router) {}

void HttpServer::run() {
  for (;;) {
    tcp::socket socket = acceptor_.accept();
    HttpSession session(std::move(socket), router_);
    session.run();
  }
}

}  // namespace messaging::http
