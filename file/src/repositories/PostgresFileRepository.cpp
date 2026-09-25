#include "file/repositories/PostgresFileRepository.hpp"

namespace file::repositories {

PostgresFileRepository::PostgresFileRepository(pqxx::connection& conn)
    : conn_(conn) {}

domain::FileRecord PostgresFileRepository::rowToFile(const pqxx::row& row) {
  domain::FileRecord f;
  f.id = row["id"].as<std::string>();
  if (!row["user_id"].is_null()) f.user_id = row["user_id"].as<std::string>();
  f.filename = row["filename"].as<std::string>();
  if (!row["mime_type"].is_null())
    f.mime_type = row["mime_type"].as<std::string>();
  f.status = row["status"].as<std::string>();
  f.size_bytes = row["size_bytes"].as<int64_t>();
  f.checksum = row["checksum"].as<std::string>();
  f.storage_ref = row["storage_ref"].as<std::string>();
  f.encryption_algorithm = row["encryption_algorithm"].as<std::string>();
  f.nonce = row["nonce"].as<std::string>();
  f.created_at = row["created_at"].as<std::string>();
  return f;
}

domain::FileRecord PostgresFileRepository::create(
    const domain::FileRecord& file) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "INSERT INTO files (user_id, filename, mime_type, status, size_bytes, "
      "                   checksum, storage_ref, encryption_algorithm, nonce) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
      "RETURNING id, user_id, filename, mime_type, status, size_bytes, "
      "          checksum, storage_ref, encryption_algorithm, nonce, created_at::text",
      file.user_id, file.filename, file.mime_type, file.status,
      file.size_bytes, file.checksum, file.storage_ref,
      file.encryption_algorithm, file.nonce);

  txn.commit();
  return rowToFile(result[0]);
}

std::optional<domain::FileRecord> PostgresFileRepository::findById(
    const std::string& id) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "SELECT id, user_id, filename, mime_type, status, size_bytes, "
      "       checksum, storage_ref, encryption_algorithm, nonce, created_at::text "
      "FROM files WHERE id = $1 AND deleted_at IS NULL",
      id);

  txn.commit();

  if (result.empty()) return std::nullopt;
  return rowToFile(result[0]);
}

bool PostgresFileRepository::updateStatus(const std::string& id,
                                           const std::string& status) {
  pqxx::work txn(conn_);
  auto result =
      txn.exec_params("UPDATE files SET status = $1 WHERE id = $2", status, id);
  txn.commit();
  return result.affected_rows() > 0;
}

bool PostgresFileRepository::softDelete(const std::string& id) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "UPDATE files SET status = 'DELETED', deleted_at = CURRENT_TIMESTAMP "
      "WHERE id = $1 AND deleted_at IS NULL",
      id);
  txn.commit();
  return result.affected_rows() > 0;
}

}  // namespace file::repositories
