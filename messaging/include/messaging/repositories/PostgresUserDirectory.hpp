#pragma once

#include <pqxx/pqxx>

#include "messaging/repositories/IUserDirectory.hpp"

namespace messaging::repositories {

class PostgresUserDirectory : public IUserDirectory {
 public:
  explicit PostgresUserDirectory(pqxx::connection& connection);

  bool exists(const std::string& userId) override;

 private:
  pqxx::connection& connection_;
};

}  // namespace messaging::repositories
