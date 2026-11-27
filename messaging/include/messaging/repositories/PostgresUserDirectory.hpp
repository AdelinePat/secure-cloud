#pragma once

#include <pqxx/pqxx>

#include "messaging/repositories/IUserDirectory.hpp"

namespace messaging::repositories {

class PostgresUserDirectory : public IUserDirectory {
 public:
  explicit PostgresUserDirectory(pqxx::connection& conn);

  std::optional<std::string> findUserIdByUsername(
      const std::string& username) override;
  bool publishIdentityKey(const std::string& userId,
                          const std::string& publicKey) override;
  std::optional<std::string> findIdentityKey(
      const std::string& userId) override;

 private:
  pqxx::connection& conn_;
};

}  // namespace messaging::repositories
