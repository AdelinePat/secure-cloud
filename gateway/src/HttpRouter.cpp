#include "gateway/HttpRouter.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "gateway/HttpResponse.hpp"

namespace gateway {

http::response<http::string_body> HttpRouter::route(
    const http::request<http::string_body>& request) {
  // Route 1: Gateway health check
  if (request.target() == "/health") {
    if (request.method() == http::verb::get) {
      return HttpResponse::health();
    }

    http::response<http::string_body> response{http::status::method_not_allowed,
                                               request.version()};

    response.set(http::field::content_type, "application/json");
    response.set(http::field::allow, "GET");
    response.body() = R"({"error":"METHOD_NOT_ALLOWED"})";
    response.prepare_payload();

    return response;
  }

  // Route 2: Proxy the Auth health check
  if (request.target() == "/auth/health") {
    if (request.method() != http::verb::get) {
      http::response<http::string_body> response{
          http::status::method_not_allowed, request.version()};

      response.set(http::field::content_type, "application/json");
      response.set(http::field::allow, "GET");
      response.body() = R"({"error":"METHOD_NOT_ALLOWED"})";
      response.prepare_payload();

      return response;
    }

    const char* authHost = std::getenv("AUTH_HOST");
    const char* authPort = std::getenv("AUTH_PORT");

    const std::string host = authHost ? authHost : "auth";
    const std::uint16_t port =
        authPort ? static_cast<std::uint16_t>(std::stoi(authPort)) : 8081;

    // Rewrite /auth/health to /health for the Auth service
    http::request<http::string_body> authRequest{http::verb::get, "/health",
                                                 request.version()};

    authRequest.set(http::field::host, host);
    authRequest.keep_alive(false);

    try {
      return httpClient_.send(host, port, authRequest);

    } catch (const std::exception& exception) {
      // Log the internal error without exposing it to the client
      std::cerr << "Auth upstream error: " << exception.what() << '\n';

      http::response<http::string_body> response{http::status::bad_gateway,
                                                 request.version()};

      response.set(http::field::content_type, "application/json");
      response.body() = R"({"error":"AUTH_SERVICE_UNAVAILABLE"})";
      response.keep_alive(false);
      response.prepare_payload();

      return response;
    }
  }

  // Route 3: Unknown endpoint
  http::response<http::string_body> response{http::status::not_found,
                                             request.version()};

  response.set(http::field::content_type, "application/json");
  response.body() = R"({"error":"NOT_FOUND"})";
  response.prepare_payload();

  return response;
}

HttpRouter::HttpRouter(HttpClient& httpClient) : httpClient_(httpClient) {}

}  // namespace gateway