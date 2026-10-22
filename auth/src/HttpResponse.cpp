#include "auth/HttpResponse.hpp"

namespace auth {

http::response<http::string_body> HttpResponse::health() {
  // Create a successful HTTP response
  http::response<http::string_body> response{http::status::ok, 11};

  // Set the response content type
  response.set(http::field::content_type, "application/json");

  // Define the JSON response body
  response.body() = R"({"status":"ok","service":"auth"})";

  // Calculate Content-Length from the response body
  response.prepare_payload();

  return response;
}

}  // namespace auth