#pragma once

#include <pqxx/pqxx>

#include "auth/repositories/IUserRepository.hpp"

namespace auth::repositories {

class PostgresUserRepository : public IUserRepository {
 public:
  // Does not own the connection — caller manages its lifetime (typically one
  // connection per service, shared across repositories).
  explicit PostgresUserRepository(pqxx::connection& conn);

  domain::User create(const domain::User& user) override;
  std::optional<domain::User> findById(const std::string& id) override;
  std::optional<domain::User> findByUsername(
      const std::string& username) override;
  bool updateUsername(const std::string& id,
                       const std::string& newUsername) override;
  bool updatePasswordHash(const std::string& id,
                           const std::string& newPasswordHash) override;
  bool remove(const std::string& id) override;

 private:
  pqxx::connection& conn_;
  static domain::User rowToUser(const pqxx::row& row);
};

}  // namespace auth::repositories
