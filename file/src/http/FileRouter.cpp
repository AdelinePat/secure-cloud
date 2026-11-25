#include "file/http/FileRouter.hpp"

#include <string_view>
#include <utility>

namespace file::http {

namespace {

Response jsonResponse(const Request& request, beast_http::status status,
                      std::string body) {
  Response response{status, request.version()};
  response.set(beast_http::field::server, "SecureChat-File");
  response.set(beast_http::field::content_type, "application/json");
  response.body() = std::move(body);
  return response;
}

Response errorResponse(const Request& request, beast_http::status status,
                       std::string_view code) {
  return jsonResponse(request, status,
                      std::string(R"({"error":")") + std::string(code) + "\"}");
}

// Request target without its query string.
std::string_view pathOf(const Request& request) {
  std::string_view target(request.target().data(), request.target().size());
  return target.substr(0, target.find('?'));
}

}  // namespace

FileRouter::FileRouter(repositories::IFileRepository& repository,
                       TokenVerifier verifyToken)
    : repository_(repository), verifyToken_(std::move(verifyToken)) {}

std::optional<std::string> FileRouter::authenticate(
    const Request& request) const {
  const auto header = request[beast_http::field::authorization];
  constexpr std::string_view prefix = "Bearer ";
  if (header.size() <= prefix.size() ||
      std::string_view(header.data(), prefix.size()) != prefix) {
    return std::nullopt;
  }
  return verifyToken_(std::string(header.substr(prefix.size())));
}

Response FileRouter::route(const Request& request) {
  const std::string_view path = pathOf(request);

  if (path == "/health") {
    if (request.method() != beast_http::verb::get) {
      return errorResponse(request, beast_http::status::method_not_allowed,
                           "METHOD_NOT_ALLOWED");
    }
    return jsonResponse(request, beast_http::status::ok,
                        R"({"status":"ok","service":"file"})");
  }

  if (path == "/files" || path.starts_with("/files/")) {
    const auto userId = authenticate(request);
    if (!userId) {
      return errorResponse(request, beast_http::status::unauthorized,
                           "UNAUTHORIZED");
    }
    // File endpoints (create, chunks, complete, download, delete) come next.
    (void)repository_;
  }

  return errorResponse(request, beast_http::status::not_found, "NOT_FOUND");
}

}  // namespace file::http
