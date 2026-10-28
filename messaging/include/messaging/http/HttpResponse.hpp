#pragma once

#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

namespace messaging::http {

namespace beast = boost::beast;

class HttpResponse {
 public:
  using Response = beast::http::response<beast::http::string_body>;

  static Response json(beast::http::status status, unsigned version,
                       const nlohmann::json& body);

  static Response error(beast::http::status status, unsigned version,
                        const std::string& code, const std::string& message);

  static Response health(unsigned version);
};

}  // namespace messaging::http
