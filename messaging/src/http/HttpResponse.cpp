#include "messaging/http/HttpResponse.hpp"

namespace messaging::http {

HttpResponse::Response HttpResponse::json(beast::http::status status,
                                          unsigned version,
                                          const nlohmann::json& body) {
  Response response{status, version};
  response.set(beast::http::field::content_type, "application/json");
  response.body() = body.dump();
  response.prepare_payload();
  return response;
}

HttpResponse::Response HttpResponse::error(beast::http::status status,
                                           unsigned version,
                                           const std::string& code,
                                           const std::string& message) {
  return json(status, version,
              {{"code", static_cast<int>(status)}, {"type", code},
               {"message", message}});
}

HttpResponse::Response HttpResponse::health(unsigned version) {
  return json(beast::http::status::ok, version,
              {{"status", "ok"}, {"service", "messaging"}});
}

}  // namespace messaging::http
