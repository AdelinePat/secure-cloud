#pragma once

#include <boost/beast/http.hpp>
#include <cstdint>
#include <string>

namespace gateway {

namespace http = boost::beast::http;

class HttpClient {
 public:
  // Send an HTTP request to an internal microservice
  http::response<http::string_body> send(
      const std::string& host, std::uint16_t port,
      const http::request<http::string_body>& request);
};

}  // namespace gateway