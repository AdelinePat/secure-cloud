#pragma once

#include <boost/beast/http.hpp>

namespace gateway {

namespace http = boost::beast::http;

class HttpResponse {
 public:
  static http::response<http::string_body> health();
};

}  // namespace gateway