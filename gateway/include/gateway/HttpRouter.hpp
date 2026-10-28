#pragma once

#include <boost/beast/http.hpp>

#include "gateway/IHttpClient.hpp"

namespace gateway {

namespace http = boost::beast::http;

class HttpRouter {
 public:
  explicit HttpRouter(IHttpClient& httpClient);
  http::response<http::string_body> route(
      const http::request<http::string_body>& request);

 private:
  IHttpClient& httpClient_;
};

}  // namespace gateway
