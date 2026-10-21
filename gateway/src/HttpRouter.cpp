#include "gateway/HttpRouter.hpp"
#include "gateway/HttpResponse.hpp"

namespace gateway {

http::response<http::string_body> HttpRouter::route(
    const http::request<http::string_body>& request) {

  if (request.target() == "/health") {

    if (request.method() == http::verb::get) {
      return HttpResponse::health();
    }

    // The route exists, but the HTTP method is not allowed
    http::response<http::string_body> response{
        http::status::method_not_allowed,
        request.version()
    };

    response.set(http::field::content_type, "application/json");
    response.set(http::field::allow, "GET");

    response.body() = R"({"error":"METHOD_NOT_ALLOWED"})";
    response.prepare_payload();

    return response;
  }

  // No route matches the requested path
  http::response<http::string_body> response{
      http::status::not_found,
      request.version()
  };

  response.set(http::field::content_type, "application/json");

  response.body() = R"({"error":"NOT_FOUND"})";
  response.prepare_payload();

  return response;
}

}  // namespace gateway