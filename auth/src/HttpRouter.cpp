#include "auth/HttpRouter.hpp"

#include "auth/HttpResponse.hpp"

namespace auth {

HttpRouter::Response HttpRouter::route(const Request& request) const {
  Response response{http::status::not_found, request.version()};

  if (request.target() == "/health") {
    if (request.method() == http::verb::get) {
      response = HttpResponse::health();
      response.version(request.version());
    } else {
      response.result(http::status::method_not_allowed);
      response.set(http::field::allow, "GET");
      response.body() = R"({"error":"Method Not Allowed"})";
    }
  } else {
    response.body() = R"({"error":"Not Found"})";
  }

  response.set(http::field::content_type, "application/json");
  response.keep_alive(request.keep_alive());
  response.prepare_payload();

  return response;
}

}  // namespace auth