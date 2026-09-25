#include "auth/repositories/PostgresSessionRepository.hpp"

namespace auth::repositories {

PostgresSessionRepository::PostgresSessionRepository(pqxx::connection& conn)
    : conn_(conn) {}

domain::Session PostgresSessionRepository::rowToSession(
    const pqxx::row& row) {
  domain::Session s;
  s.id = row["id"].as<std::string>();
  s.user_id = row["user_id"].as<std::string>();
  s.refresh_token_hash = row["refresh_token_hash"].as<std::string>();
  s.issued_at = row["issued_at"].as<std::string>();
  s.expires_at = row["expires_at"].as<std::string>();
  if (!row["revoked_at"].is_null())
    s.revoked_at = row["revoked_at"].as<std::string>();
  return s;
}

domain::Session PostgresSessionRepository::create(
    const domain::Session& session) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "INSERT INTO sessions (user_id, refresh_token_hash, expires_at) "
      "VALUES ($1, $2, $3::timestamptz) "
      "RETURNING id, user_id, refresh_token_hash, issued_at::text, "
      "          expires_at::text, revoked_at::text",
      session.user_id, session.refresh_token_hash, session.expires_at);

  txn.commit();
  return rowToSession(result[0]);
}

std::optional<domain::Session>
PostgresSessionRepository::findByRefreshTokenHash(const std::string& hash) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "SELECT id, user_id, refresh_token_hash, issued_at::text, "
      "       expires_at::text, revoked_at::text "
      "FROM sessions WHERE refresh_token_hash = $1",
      hash);

  txn.commit();

  if (result.empty()) return std::nullopt;
  return rowToSession(result[0]);
}

bool PostgresSessionRepository::revoke(const std::string& sessionId) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP "
      "WHERE id = $1 AND revoked_at IS NULL",
      sessionId);
  txn.commit();
  return result.affected_rows() > 0;
}

int PostgresSessionRepository::revokeAllForUser(const std::string& userId) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP "
      "WHERE user_id = $1 AND revoked_at IS NULL",
      userId);
  txn.commit();
  return static_cast<int>(result.affected_rows());
}

}  // namespace auth::repositories
