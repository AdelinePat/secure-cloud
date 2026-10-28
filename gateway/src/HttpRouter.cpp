#include "gateway/HttpRouter.hpp"

#include <exception>
#include <iostream>
#include <string>

#include "gateway/HttpResponse.hpp"
#include "gateway/ServiceConfig.hpp"

namespace gateway {

namespace {

using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

Response jsonError(http::status status, unsigned version,
                   const std::string& code) {
  Response response{status, version};
  response.set(http::field::content_type, "application/json");
  response.body() = R"({"error":")" + code + R"("})";
  response.keep_alive(false);
  response.prepare_payload();
  return response;
}

Response methodNotAllowed(unsigned version, const std::string& allow) {
  Response response{http::status::method_not_allowed, version};
  response.set(http::field::content_type, "application/json");
  response.set(http::field::allow, allow);
  response.body() = R"({"error":"METHOD_NOT_ALLOWED"})";
  response.keep_alive(false);
  response.prepare_payload();
  return response;
}

bool hasPathPrefix(const std::string& target, const std::string& prefix) {
  if (target.compare(0, prefix.size(), prefix) != 0) return false;
  if (target.size() == prefix.size()) return true;

  const char next = target[prefix.size()];
  return next == '/' || next == '?';
}

Response proxy(IHttpClient& httpClient, const Request& request,
               const std::string& host, std::uint16_t port,
               const std::string& target, const std::string& unavailableCode,
               bool stripCredentials) {
  Request upstream = request;
  upstream.target(target);
  upstream.set(http::field::host, host);
  upstream.keep_alive(false);

  if (stripCredentials) {
    upstream.erase(http::field::authorization);
    upstream.erase(http::field::cookie);
  }

  try {
    return httpClient.send(host, port, upstream);

  } catch (const std::exception& exception) {
    // Log the internal error without exposing it to the client.
    std::cerr << "Upstream error (" << host << "): " << exception.what()
              << '\n';
    return jsonError(http::status::bad_gateway, request.version(),
                     unavailableCode);
  }
}

}  // namespace

http::response<http::string_body> HttpRouter::route(
    const http::request<http::string_body>& request) {
  const std::string target(request.target());

  // Route 1: Gateway health check.
  if (target == "/health") {
    if (request.method() == http::verb::get) {
      return HttpResponse::health();
    }
    return methodNotAllowed(request.version(), "GET");
  }

  // Route 2: Proxy the Auth health check.
  if (target == "/auth/health") {
    if (request.method() != http::verb::get) {
      return methodNotAllowed(request.version(), "GET");
    }

    try {
      const auto auth = ServiceConfig::auth();
      return proxy(httpClient_, request, auth.host, auth.port, "/health",
                   "AUTH_SERVICE_UNAVAILABLE", true);
    } catch (const std::exception& exception) {
      std::cerr << "Auth configuration error: " << exception.what() << '\n';
      return jsonError(http::status::bad_gateway, request.version(),
                       "AUTH_SERVICE_UNAVAILABLE");
    }
  }

  // Route 3: Proxy Auth login/refresh. Both the legacy /auth/... paths and the
  // documented /secureChat/auth/... paths are accepted.
  const bool isAuthLogin =
      target == "/auth/login" || target == "/secureChat/auth/login";
  const bool isAuthRefresh =
      target == "/auth/refresh" || target == "/secureChat/auth/refresh";

  if (isAuthLogin || isAuthRefresh) {
    try {
      const auto auth = ServiceConfig::auth();
      const std::string upstreamTarget =
          isAuthLogin ? "/auth/login" : "/auth/refresh";

      return proxy(httpClient_, request, auth.host, auth.port, upstreamTarget,
                   "AUTH_SERVICE_UNAVAILABLE", true);
    } catch (const std::exception& exception) {
      std::cerr << "Auth configuration error: " << exception.what() << '\n';
      return jsonError(http::status::bad_gateway, request.version(),
                       "AUTH_SERVICE_UNAVAILABLE");
    }
  }

  // Route 4: Proxy protected messaging routes. The client only ever talks to
  // the gateway; the gateway forwards the bearer token so messaging can
  // authenticate and authorise the caller.
  if (hasPathPrefix(target, "/secureChat/conversations")) {
    try {
      const auto messaging = ServiceConfig::messaging();

      // /secureChat/conversations/{id}/... -> /conversations/{id}/...
      const std::string upstreamTarget = target.substr(
          std::string("/secureChat").size());

      return proxy(httpClient_, request, messaging.host, messaging.port,
                   upstreamTarget, "MESSAGING_SERVICE_UNAVAILABLE", false);
    } catch (const std::exception& exception) {
      std::cerr << "Messaging configuration error: " << exception.what()
                << '\n';
      return jsonError(http::status::bad_gateway, request.version(),
                       "MESSAGING_SERVICE_UNAVAILABLE");
    }
  }

  // Route 5: Unknown endpoint.
  return jsonError(http::status::not_found, request.version(), "NOT_FOUND");
}

HttpRouter::HttpRouter(IHttpClient& httpClient) : httpClient_(httpClient) {}

}  // namespace gateway
