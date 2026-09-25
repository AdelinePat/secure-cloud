#pragma once

#include <optional>
#include <string>

#include "auth/domain/User.hpp"

namespace auth::repositories {

// Business logic (AuthService, etc.) depends on this interface only, never on
// PostgresUserRepository directly, so it can be unit tested against
// InMemoryUserRepository with no live database.
class IUserRepository {
 public:
  virtual ~IUserRepository() = default;

  // Inserts a new user. `user.id`/created_at/updated_at are ignored on input
  // and populated by the store. Throws on unique constraint violation
  // (duplicate username/email) — callers should catch and translate to a 409.
  virtual domain::User create(const domain::User& user) = 0;

  virtual std::optional<domain::User> findById(const std::string& id) = 0;
  virtual std::optional<domain::User> findByUsername(
      const std::string& username) = 0;

  // Returns false if no row matched (user doesn't exist).
  virtual bool updateUsername(const std::string& id,
                               const std::string& newUsername) = 0;
  virtual bool updatePasswordHash(const std::string& id,
                                   const std::string& newPasswordHash) = 0;

  virtual bool remove(const std::string& id) = 0;
};

}  // namespace auth::repositories
