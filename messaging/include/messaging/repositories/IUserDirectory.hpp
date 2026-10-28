#pragma once

#include <string>

namespace messaging::repositories {

// Read-only view of the users owned by the auth service. Messaging and auth
// share the same PostgreSQL instance, so participant validation is a direct
// read; no synchronous service-to-service call is needed for the MVP.
class IUserDirectory {
 public:
  virtual ~IUserDirectory() = default;

  virtual bool exists(const std::string& userId) = 0;
};

}  // namespace messaging::repositories
