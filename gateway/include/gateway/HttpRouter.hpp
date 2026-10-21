#pragma once

#include <boost/beast/http.hpp>

namespace gateway {

namespace http = boost::beast::http;

class HttpRouter {
 public:
  static http::response<http::string_body> route(
      const http::request<http::string_body>& request);
};

}  // namespace gateway