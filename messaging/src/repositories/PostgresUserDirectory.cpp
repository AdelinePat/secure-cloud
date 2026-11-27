#include "messaging/repositories/PostgresUserDirectory.hpp"

namespace messaging::repositories {

PostgresUserDirectory::PostgresUserDirectory(pqxx::connection& conn)
    : conn_(conn) {}

std::optional<std::string> PostgresUserDirectory::findUserIdByUsername(
    const std::string& username) {
  pqxx::work txn(conn_);
  const auto rows =
      txn.exec_params("SELECT id FROM users WHERE username = $1", username);
  txn.commit();

  if (rows.empty()) {
    return std::nullopt;
  }
  return rows[0]["id"].as<std::string>();
}

bool PostgresUserDirectory::publishIdentityKey(const std::string& userId,
                                               const std::string& publicKey) {
  pqxx::work txn(conn_);
  const auto rows = txn.exec_params(
      "UPDATE users SET public_key = $1 WHERE id = $2 "
      "AND (public_key = '' OR public_key = $1)",
      publicKey, userId);
  txn.commit();
  return rows.affected_rows() == 1;
}

std::optional<std::string> PostgresUserDirectory::findIdentityKey(
    const std::string& userId) {
  pqxx::work txn(conn_);
  const auto rows =
      txn.exec_params("SELECT public_key FROM users WHERE id = $1", userId);
  txn.commit();

  if (rows.empty() || rows[0][0].is_null()) {
    return std::nullopt;
  }
  auto key = rows[0][0].as<std::string>();
  if (key.empty()) {
    return std::nullopt;
  }
  return key;
}

}  // namespace messaging::repositories
