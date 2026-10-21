#include "gateway/HttpResponse.hpp"

namespace gateway {

http::response<http::string_body> HttpResponse::health() {
  // Create an HTTP 200 OK response
  http::response<http::string_body> response{
      http::status::ok,
      11
  };

  // Define the response headers
  response.set(http::field::server, "SecureChat Gateway");
  response.set(http::field::content_type, "application/json");

  // Define the JSON response body
  response.body() = R"({"status":"ok","service":"gateway"})";

  // Automatically calculate Content-Length
  response.prepare_payload();

  return response;
}

}  // namespace gateway