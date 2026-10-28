#include "messaging/http/HttpSession.hpp"

#include <utility>

namespace messaging::http {

HttpSession::HttpSession(asio::ip::tcp::socket socket, HttpRouter& router)
    : stream_(std::move(socket)), router_(router) {}

void HttpSession::run() {
  beast::error_code ec;

  http::read(stream_, buffer_, request_, ec);
  if (ec) return;

  response_ = router_.route(request_);
  response_.keep_alive(false);

  http::write(stream_, response_, ec);
  stream_.socket().shutdown(asio::ip::tcp::socket::shutdown_both, ec);
}

}  // namespace messaging::http
