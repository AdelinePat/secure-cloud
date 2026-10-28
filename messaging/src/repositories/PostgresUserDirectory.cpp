#include "messaging/repositories/PostgresUserDirectory.hpp"

namespace messaging::repositories {

PostgresUserDirectory::PostgresUserDirectory(pqxx::connection& connection)
    : connection_(connection) {}

bool PostgresUserDirectory::exists(const std::string& userId) {
  try {
    pqxx::work txn(connection_);
    const auto rows = txn.exec_params("SELECT 1 FROM users WHERE id = $1",
                                      userId);
    txn.commit();
    return !rows.empty();
  } catch (const pqxx::sql_error&) {
    // Malformed UUIDs are simply "not found" from the API's point of view.
    return false;
  }
}

}  // namespace messaging::repositories
