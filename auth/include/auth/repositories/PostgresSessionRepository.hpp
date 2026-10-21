#pragma once

#include <pqxx/pqxx>

#include "auth/repositories/ISessionRepository.hpp"

namespace auth::repositories {

class PostgresSessionRepository : public ISessionRepository {
 public:
  explicit PostgresSessionRepository(pqxx::connection& conn);

  domain::Session create(const domain::Session& session) override;
  std::optional<domain::Session> findByRefreshTokenHash(
      const std::string& hash) override;
  bool revoke(const std::string& sessionId) override;
  int revokeAllForUser(const std::string& userId) override;

 private:
  pqxx::connection& conn_;
  static domain::Session rowToSession(const pqxx::row& row);
};

}  // namespace auth::repositories
