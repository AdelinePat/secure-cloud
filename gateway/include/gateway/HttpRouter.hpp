#pragma once

#include <boost/beast/http.hpp>

#include "gateway/HttpClient.hpp"

namespace gateway {

namespace http = boost::beast::http;

class HttpRouter {
 public:
  explicit HttpRouter(HttpClient& httpClient);
  http::response<http::string_body> route(
      const http::request<http::string_body>& request);

 private:
  HttpClient& httpClient_;
};

}  // namespace gateway