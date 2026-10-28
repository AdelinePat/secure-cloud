#pragma once

#include <boost/beast/http.hpp>

#include "messaging/crypto/JwtVerifier.hpp"
#include "messaging/services/MessagingService.hpp"

namespace messaging::http {

namespace beast = boost::beast;

class HttpRouter {
 public:
  using Request = beast::http::request<beast::http::string_body>;
  using Response = beast::http::response<beast::http::string_body>;

  HttpRouter(services::MessagingService& messagingService,
             crypto::IJwtVerifier& jwtVerifier);

  Response route(const Request& request) const;

 private:
  Response routeAuthenticated(const Request& request,
                              const std::string& userId) const;

  services::MessagingService& messagingService_;
  crypto::IJwtVerifier& jwtVerifier_;
};

}  // namespace messaging::http
