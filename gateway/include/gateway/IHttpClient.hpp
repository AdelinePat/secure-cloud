#pragma once

#include <boost/beast/http.hpp>
#include <cstdint>
#include <string>

namespace gateway {

namespace http = boost::beast::http;

class IHttpClient {
 public:
  virtual ~IHttpClient() = default;

  virtual http::response<http::string_body> send(
      const std::string& host, std::uint16_t port,
      const http::request<http::string_body>& request) = 0;
};

}  // namespace gateway
