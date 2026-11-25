#pragma once

#include <boost/beast/http.hpp>
#include <cstddef>
#include <string_view>

#include "gateway/HttpClient.hpp"

namespace gateway {

namespace http = boost::beast::http;

// Largest request body for most routes (Beast's default).
inline constexpr std::size_t kDefaultBodyLimit = 1024 * 1024;

// Largest request body for /files routes: a 1 MiB ciphertext chunk plus
// encryption overhead, with headroom. Matches the file service's own limit.
inline constexpr std::size_t kFileBodyLimit = 2 * 1024 * 1024;

class HttpRouter {
 public:
  explicit HttpRouter(HttpClient& httpClient);

  // /files and /files/... (query string ignored), proxied to the file service.
  static bool isFileRoute(std::string_view target);

  // Body limit to apply once the request target is known.
  static std::size_t bodyLimitFor(std::string_view target);

  http::response<http::string_body> route(
      const http::request<http::string_body>& request);

 private:
  HttpClient& httpClient_;
};

}  // namespace gateway