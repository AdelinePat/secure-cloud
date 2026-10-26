#include "auth/HttpSession.hpp"

#include <utility>

namespace auth {

HttpSession::HttpSession(tcp::socket socket, HttpRouter& router)
    : stream_(std::move(socket)), router_(router) {}

void HttpSession::run() {
  beast::error_code ec;

  // Read the incoming HTTP request
  http::read(stream_, buffer_, request_, ec);

  if (ec) {
    return;
  }

  // Dispatch the request to the router
  response_ = router_.route(request_);

  // Send the HTTP response
  http::write(stream_, response_, ec);

  // Close the TCP connection
  stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
}

}  // namespace auth