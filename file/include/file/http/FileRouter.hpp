#pragma once

#include <functional>
#include <optional>
#include <string>

#include "file/http/HttpServer.hpp"
#include "file/repositories/IFileRepository.hpp"

namespace file::http {

// Returns the authenticated user id for a bearer token, or nullopt.
// In production this is common::jwt::JwtVerifier::verify; tests stub it.
using TokenVerifier =
    std::function<std::optional<std::string>(const std::string& token)>;

// Maps HTTP requests to file operations. Paths are the file service's
// internal ones (the gateway strips its public prefix).
class FileRouter {
 public:
  FileRouter(repositories::IFileRepository& repository,
             TokenVerifier verifyToken);

  Response route(const Request& request);

 private:
  // User id from "Authorization: Bearer <jwt>", or nullopt.
  std::optional<std::string> authenticate(const Request& request) const;

  repositories::IFileRepository& repository_;
  TokenVerifier verifyToken_;
};

}  // namespace file::http
