#pragma once

#include <boost/beast/http.hpp>

#include "auth/services/AuthService.hpp"

namespace common::audit {
class Client;
}

namespace auth {

namespace http = boost::beast::http;

class HttpRouter {
 public:
  using Request = http::request<http::string_body>;
  using Response = http::response<http::string_body>;

  // `audit` is optional: when null or disabled, no audit events are emitted.
  // Unit tests construct the router without one.
  explicit HttpRouter(services::AuthService& authService,
                      common::audit::Client* audit = nullptr);

  Response route(const Request& request) const;

 private:
  services::AuthService& authService_;
  common::audit::Client* audit_ = nullptr;
};

}  // namespace auth