#pragma once

#include <optional>
#include <string>

#include "auth/domain/Session.hpp"

namespace auth::repositories {

class ISessionRepository {
 public:
  virtual ~ISessionRepository() = default;

  virtual domain::Session create(const domain::Session& session) = 0;

  // Used at refresh time: look up the session behind a presented refresh token.
  virtual std::optional<domain::Session> findByRefreshTokenHash(
      const std::string& hash) = 0;

  // Marks a single session revoked. Returns false if not found.
  virtual bool revoke(const std::string& sessionId) = 0;

  // Marks every non-revoked session for a user as revoked (e.g. "log out
  // everywhere" / emergency revocation). Returns the number of sessions revoked.
  virtual int revokeAllForUser(const std::string& userId) = 0;
};

}  // namespace auth::repositories
