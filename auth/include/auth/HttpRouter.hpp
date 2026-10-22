#pragma once

#include <boost/beast/http.hpp>

namespace auth {

namespace http = boost::beast::http;

class HttpRouter {
 public:
  using Request = http::request<http::string_body>;
  using Response = http::response<http::string_body>;

  Response route(const Request& request) const;
};

}  // namespace auth