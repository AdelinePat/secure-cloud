
#include "auth/HttpRouter.hpp"

#include <exception>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>

#include "auth/HttpResponse.hpp"

namespace auth {

namespace {

using json = nlohmann::json;

// Build a JSON HTTP response.
HttpRouter::Response jsonResponse(http::status status, unsigned version,
                                  const json& body) {
  HttpRouter::Response response{status, version};

  response.set(http::field::content_type, "application/json");
  response.body() = body.dump();
  response.prepare_payload();

  return response;
}

}  // namespace

HttpRouter::HttpRouter(services::AuthService& authService)
    : authService_(authService) {}

HttpRouter::Response HttpRouter::route(const Request& request) const {
  Response response{http::status::not_found, request.version()};

  if (request.target() == "/health") {
    if (request.method() == http::verb::get) {
      response = HttpResponse::health();
      response.version(request.version());

    } else {
      response =
          jsonResponse(http::status::method_not_allowed, request.version(),
                       {{"error", "Method Not Allowed"}});

      response.set(http::field::allow, "GET");
    }

  } else if (request.target() == "/auth/login") {
    if (request.method() != http::verb::post) {
      response =
          jsonResponse(http::status::method_not_allowed, request.version(),
                       {{"error", "Method Not Allowed"}});

      response.set(http::field::allow, "POST");

    } else {
      try {
        const json body = json::parse(request.body());

        if (!body.is_object() || !body.contains("username") ||
            !body.contains("password") || !body["username"].is_string() ||
            !body["password"].is_string() ||
            body["username"].get<std::string>().empty() ||
            body["password"].get<std::string>().empty()) {
          response = jsonResponse(http::status::bad_request, request.version(),
                                  {{"error", "Invalid login request"}});

        } else {
          services::LoginRequest loginRequest{
              body["username"].get<std::string>(),
              body["password"].get<std::string>()};

          const auto result = authService_.login(loginRequest);

          if (!result.has_value()) {
            response =
                jsonResponse(http::status::unauthorized, request.version(),
                             {{"error", "Invalid credentials"}});

          } else {
            response = jsonResponse(http::status::ok, request.version(),
                                    {{"access_token", result->accessToken},
                                     {"refresh_token", result->refreshToken},
                                     {"token_type", "Bearer"},
                                     {"expires_in", 900}});
          }
        }

      } catch (const json::exception&) {
        response = jsonResponse(http::status::bad_request, request.version(),
                                {{"error", "Invalid JSON request"}});

      } catch (const std::exception& exception) {
        // Never expose internal errors or credentials to HTTP clients.
        std::cerr << "Auth login error: " << exception.what() << '\n';

        response =
            jsonResponse(http::status::internal_server_error, request.version(),
                         {{"error", "Internal Server Error"}});
      }
    }

  } else if (request.target() == "/auth/refresh") {
    if (request.method() != http::verb::post) {
      response =
          jsonResponse(http::status::method_not_allowed, request.version(),
                       {{"error", "Method Not Allowed"}});

      response.set(http::field::allow, "POST");

    } else {
      try {
        const json body = json::parse(request.body());

        // Validate the refresh request.
        if (!body.is_object() || !body.contains("refresh_token") ||
            !body["refresh_token"].is_string() ||
            body["refresh_token"].get<std::string>().empty()) {
          response = jsonResponse(http::status::bad_request, request.version(),
                                  {{"error", "Invalid refresh request"}});

        } else {
          services::RefreshRequest refreshRequest{
              body["refresh_token"].get<std::string>()};

          // Attempt to rotate the refresh token.
          const auto result = authService_.refresh(refreshRequest);

          if (!result.has_value()) {
            response =
                jsonResponse(http::status::unauthorized, request.version(),
                             {{"error", "Invalid refresh token"}});

          } else {
            response = jsonResponse(http::status::ok, request.version(),
                                    {{"access_token", result->accessToken},
                                     {"refresh_token", result->refreshToken},
                                     {"token_type", "Bearer"},
                                     {"expires_in", 900}});
          }
        }

      } catch (const json::exception&) {
        response = jsonResponse(http::status::bad_request, request.version(),
                                {{"error", "Invalid JSON request"}});

      } catch (const std::exception& exception) {
        // Never expose internal errors or tokens to HTTP clients.
        std::cerr << "Auth refresh error: " << exception.what() << '\n';

        response =
            jsonResponse(http::status::internal_server_error, request.version(),
                         {{"error", "Internal Server Error"}});
      }
    }

  } else {
    response = jsonResponse(http::status::not_found, request.version(),
                            {{"error", "Not Found"}});
  }

  response.set(http::field::content_type, "application/json");
  response.keep_alive(request.keep_alive());
  response.prepare_payload();

  return response;
}

}  // namespace auth
