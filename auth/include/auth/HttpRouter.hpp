#pragma once

#include <boost/beast/http.hpp>

#include "auth/services/AuthService.hpp"

namespace auth {

namespace http = boost::beast::http;

class HttpRouter {
 public:
  using Request = http::request<http::string_body>;
  using Response = http::response<http::string_body>;

  explicit HttpRouter(services::AuthService& authService);

  Response route(const Request& request) const;

 private:
  services::AuthService& authService_;
};

}  // namespace auth