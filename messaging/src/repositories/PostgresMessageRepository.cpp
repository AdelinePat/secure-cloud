#include "messaging/repositories/PostgresMessageRepository.hpp"

namespace messaging::repositories {

namespace {

// ciphertext is arbitrary binary data. Rather than depend on the exact
// bytea-binding API of whichever libpqxx version dnf pulls in (this varies
// across 7.x releases), we round-trip it as hex through decode()/encode() —
// slightly more bytes over the wire, but works identically on every libpqxx
// version and is trivial to verify by eye when debugging.
std::string toHex(const std::vector<unsigned char>& bytes) {
  static const char* digits = "0123456789abcdef";
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char b : bytes) {
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 0x0F]);
  }
  return out;
}

std::vector<unsigned char> fromHex(const std::string& hex) {
  std::vector<unsigned char> out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(
        static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

}  // namespace

PostgresMessageRepository::PostgresMessageRepository(pqxx::connection& conn)
    : conn_(conn) {}

domain::Message PostgresMessageRepository::create(
    const domain::Message& message) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      "INSERT INTO messages (id, conversation_id, user_id, ciphertext, nonce, encryption_version) "
      "VALUES ($1, $2, $3, decode($4, 'hex'), $5, $6) "
      "RETURNING id, conversation_id, user_id, encode(ciphertext, 'hex') AS ciphertext_hex, "
      "          nonce, encryption_version, server_timestamp::text",
      message.id, message.conversation_id, message.user_id,
      toHex(message.ciphertext), message.nonce, message.encryption_version);

  txn.commit();

  const auto& row = result[0];
  domain::Message stored;
  stored.id = row["id"].as<std::string>();
  stored.conversation_id = row["conversation_id"].as<std::string>();
  if (!row["user_id"].is_null()) stored.user_id = row["user_id"].as<std::string>();
  stored.ciphertext = fromHex(row["ciphertext_hex"].as<std::string>());
  stored.nonce = row["nonce"].as<std::string>();
  stored.encryption_version = row["encryption_version"].as<int16_t>();
  stored.server_timestamp = row["server_timestamp"].as<std::string>();
  return stored;
}

MessagePage PostgresMessageRepository::history(
    const std::string& conversationId, std::optional<std::string> before,
    int limit) {
  pqxx::work txn(conn_);

  pqxx::result result;
  if (before.has_value()) {
    result = txn.exec_params(
        "SELECT id, conversation_id, user_id, encode(ciphertext, 'hex') AS ciphertext_hex, "
        "       nonce, encryption_version, server_timestamp::text "
        "FROM messages "
        "WHERE conversation_id = $1 AND server_timestamp < $2::timestamptz AND deleted_at IS NULL "
        "ORDER BY server_timestamp DESC LIMIT $3",
        conversationId, *before, limit);
  } else {
    result = txn.exec_params(
        "SELECT id, conversation_id, user_id, encode(ciphertext, 'hex') AS ciphertext_hex, "
        "       nonce, encryption_version, server_timestamp::text "
        "FROM messages "
        "WHERE conversation_id = $1 AND deleted_at IS NULL "
        "ORDER BY server_timestamp DESC LIMIT $2",
        conversationId, limit);
  }

  txn.commit();

  MessagePage page;
  for (const auto& row : result) {
    domain::Message m;
    m.id = row["id"].as<std::string>();
    m.conversation_id = row["conversation_id"].as<std::string>();
    if (!row["user_id"].is_null()) m.user_id = row["user_id"].as<std::string>();
    m.ciphertext = fromHex(row["ciphertext_hex"].as<std::string>());
    m.nonce = row["nonce"].as<std::string>();
    m.encryption_version = row["encryption_version"].as<int16_t>();
    m.server_timestamp = row["server_timestamp"].as<std::string>();
    page.messages.push_back(std::move(m));
  }

  // Only offer a next page if we got a full page — a short page means we hit
  // the end of history.
  if (static_cast<int>(page.messages.size()) == limit) {
    page.nextCursor = page.messages.back().server_timestamp;
  }

  return page;
}

}  // namespace messaging::repositories
