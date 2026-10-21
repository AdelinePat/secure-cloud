#include "auth/repositories/PostgresUserRepository.hpp"

namespace auth::repositories {

PostgresUserRepository::PostgresUserRepository(pqxx::connection& conn)
    : conn_(conn) {}

domain::User PostgresUserRepository::rowToUser(const pqxx::row& row) {
  domain::User u;
  u.id = row["id"].as<std::string>();
  u.username = row["username"].as<std::string>();
  u.email = row["email"].as<std::string>();
  u.password_hash = row["password_hash"].as<std::string>();
  u.public_key = row["public_key"].as<std::string>();
  u.key_algorithm = row["key_algorithm"].as<std::string>();
  u.created_at = row["created_at"].as<std::string>();
  u.updated_at = row["updated_at"].as<std::string>();
  return u;
}

domain::User PostgresUserRepository::create(const domain::User& user) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "INSERT INTO users (username, email, password_hash, public_key, key_algorithm) "
      "VALUES ($1, $2, $3, $4, $5) "
      "RETURNING id, username, email, password_hash, public_key, key_algorithm, "
      "          created_at::text, updated_at::text",
      user.username, user.email, user.password_hash, user.public_key,
      user.key_algorithm);

  txn.commit();
  return rowToUser(result[0]);
}

std::optional<domain::User> PostgresUserRepository::findById(
    const std::string& id) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "SELECT id, username, email, password_hash, public_key, key_algorithm, "
      "       created_at::text, updated_at::text "
      "FROM users WHERE id = $1",
      id);

  txn.commit();

  if (result.empty()) return std::nullopt;
  return rowToUser(result[0]);
}

std::optional<domain::User> PostgresUserRepository::findByUsername(
    const std::string& username) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "SELECT id, username, email, password_hash, public_key, key_algorithm, "
      "       created_at::text, updated_at::text "
      "FROM users WHERE username = $1",
      username);

  txn.commit();

  if (result.empty()) return std::nullopt;
  return rowToUser(result[0]);
}

bool PostgresUserRepository::updateUsername(const std::string& id,
                                             const std::string& newUsername) {
  pqxx::work txn(conn_);
  auto result =
      txn.exec_params("UPDATE users SET username = $1 WHERE id = $2",
                       newUsername, id);
  txn.commit();
  return result.affected_rows() > 0;
}

bool PostgresUserRepository::updatePasswordHash(
    const std::string& id, const std::string& newPasswordHash) {
  pqxx::work txn(conn_);
  auto result =
      txn.exec_params("UPDATE users SET password_hash = $1 WHERE id = $2",
                       newPasswordHash, id);
  txn.commit();
  return result.affected_rows() > 0;
}

bool PostgresUserRepository::remove(const std::string& id) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params("DELETE FROM users WHERE id = $1", id);
  txn.commit();
  return result.affected_rows() > 0;
}

}  // namespace auth::repositories
