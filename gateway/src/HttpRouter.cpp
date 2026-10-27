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

  // Route 4: Proxy the Messaging health check.
  if (request.target() == "/messaging/health") {
    if (request.method() != http::verb::get) {
      http::response<http::string_body> response{
          http::status::method_not_allowed, request.version()};

      response.set(http::field::content_type, "application/json");
      response.set(http::field::allow, "GET");
      response.body() = R"({"error":"METHOD_NOT_ALLOWED"})";
      response.prepare_payload();

      return response;
    }

    const char* messagingHost = std::getenv("MESSAGING_HOST");
    const char* messagingPort = std::getenv("MESSAGING_PORT");

    const std::string host = messagingHost ? messagingHost : "messaging";
    const std::uint16_t port =
        messagingPort ? static_cast<std::uint16_t>(std::stoi(messagingPort))
                      : 8082;

    // Rewrite the public route to the Messaging internal route.
    http::request<http::string_body> messagingRequest{
        http::verb::get, "/health", request.version()};

    messagingRequest.set(http::field::host, host);
    messagingRequest.keep_alive(false);

    try {
      return httpClient_.send(host, port, messagingRequest);

    } catch (const std::exception& exception) {
      std::cerr << "Messaging upstream error: " << exception.what() << '\n';

      http::response<http::string_body> response{http::status::bad_gateway,
                                                 request.version()};

      response.set(http::field::content_type, "application/json");
      response.body() = R"({"error":"MESSAGING_SERVICE_UNAVAILABLE"})";
      response.keep_alive(false);
      response.prepare_payload();

      return response;
    }
  }

  // Route 3: Proxy Auth login and refresh requests.
  if (request.target() == "/auth/login" ||
      request.target() == "/auth/refresh") {
    const char* authHost = std::getenv("AUTH_HOST");
    const char* authPort = std::getenv("AUTH_PORT");

    const std::string host = authHost ? authHost : "auth";
    const std::uint16_t port =
        authPort ? static_cast<std::uint16_t>(std::stoi(authPort)) : 8081;

    // Forward the original request to the Auth service.
    http::request<http::string_body> authRequest = request;

    authRequest.set(http::field::host, host);
    authRequest.keep_alive(false);
    authRequest.erase(http::field::authorization);
    authRequest.erase(http::field::cookie);

    try {
      return httpClient_.send(host, port, authRequest);

    } catch (const std::exception& exception) {
      // Log internal errors without exposing them to clients.
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