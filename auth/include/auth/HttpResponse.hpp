#pragma once

#include <boost/beast/http.hpp>

namespace auth {

namespace http = boost::beast::http;

class HttpResponse {
 public:
  static http::response<http::string_body> health();
};

}  // namespace auth