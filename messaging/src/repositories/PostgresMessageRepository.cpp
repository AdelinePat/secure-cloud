#include "messaging/repositories/PostgresMessageRepository.hpp"

#include <string>
#include <utility>

namespace messaging::repositories {

namespace {

// ciphertext is arbitrary binary data stored in a text column as hex. Keeping
// it as a plain string avoids depending on the exact bytea-binding API of
// whichever libpqxx version dnf pulls in (this varies across 7.x releases),
// and it is trivial to verify by eye when debugging.
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

// Opaque pagination cursor: "<server_timestamp>|<message_id>". Keeping the id
// in the cursor makes pagination stable when two messages share a timestamp.
constexpr const char* kMaxUuid = "ffffffff-ffff-ffff-ffff-ffffffffffff";

std::pair<std::string, std::string> parseCursor(const std::string& cursor) {
  const auto separator = cursor.find('|');
  if (separator == std::string::npos) {
    // Backwards compatibility with the timestamp-only cursor.
    return {cursor, kMaxUuid};
  }
  return {cursor.substr(0, separator), cursor.substr(separator + 1)};
}

}  // namespace

PostgresMessageRepository::PostgresMessageRepository(pqxx::connection& conn)
    : conn_(conn) {}

std::vector<domain::Attachment> PostgresMessageRepository::fetchAttachments(
    pqxx::work& txn, const std::string& messageId) {
  const auto rows = txn.exec_params(
      "SELECT file_id, size_bytes, COALESCE(checksum_sha256, '') AS checksum "
      "FROM message_attachments WHERE message_id = $1",
      messageId);

  std::vector<domain::Attachment> attachments;
  for (const auto& row : rows) {
    attachments.push_back(domain::Attachment{
        row["file_id"].as<std::string>(),
        row["size_bytes"].as<long long>(),
        row["checksum"].as<std::string>()});
  }
  return attachments;
}

domain::Message PostgresMessageRepository::create(
    const domain::Message& message) {
  try {
    pqxx::work txn(conn_);

    auto result = txn.exec_params(
        "INSERT INTO messages "
        "  (id, conversation_id, user_id, ciphertext, nonce, "
        "   encryption_version, client_timestamp) "
        "VALUES ($1, $2, $3, $4, $5, $6, "
        "        COALESCE(NULLIF($7, '')::timestamptz, CURRENT_TIMESTAMP)) "
        "RETURNING id, conversation_id, user_id, "
        "          ciphertext AS ciphertext_hex, nonce, "
        "          encryption_version, client_timestamp::text, "
        "          server_timestamp::text",
        message.id, message.conversation_id, message.user_id,
        toHex(message.ciphertext), message.nonce, message.encryption_version,
        message.client_timestamp);

    for (const auto& attachment : message.attachments) {
      txn.exec_params(
          "INSERT INTO message_attachments "
          "  (message_id, file_id, size_bytes, checksum_sha256) "
          "VALUES ($1, $2, $3, $4) "
          "ON CONFLICT (message_id, file_id) DO NOTHING",
          message.id, attachment.file_id, attachment.size_bytes,
          attachment.checksum_sha256);
    }

    txn.commit();

    const auto& row = result[0];
    domain::Message stored;
    stored.id = row["id"].as<std::string>();
    stored.conversation_id = row["conversation_id"].as<std::string>();
    if (!row["user_id"].is_null())
      stored.user_id = row["user_id"].as<std::string>();
    stored.ciphertext = fromHex(row["ciphertext_hex"].as<std::string>());
    stored.nonce = row["nonce"].as<std::string>();
    stored.encryption_version = row["encryption_version"].as<int16_t>();
    stored.client_timestamp = row["client_timestamp"].as<std::string>();
    stored.server_timestamp = row["server_timestamp"].as<std::string>();
    stored.attachments = message.attachments;
    return stored;

  } catch (const pqxx::sql_error& error) {
    if (std::string(error.sqlstate()) == "23505") {
      throw DuplicateMessageError("duplicate message id");
    }
    throw;
  }
}

MessagePage PostgresMessageRepository::history(
    const std::string& conversationId, std::optional<std::string> before,
    int limit) {
  pqxx::work txn(conn_);

  pqxx::result result;
  if (before.has_value()) {
    const auto [timestamp, messageId] = parseCursor(*before);
    result = txn.exec_params(
        "SELECT id, conversation_id, user_id, "
        "       ciphertext AS ciphertext_hex, nonce, "
        "       encryption_version, client_timestamp::text, "
        "       server_timestamp::text "
        "FROM messages "
        "WHERE conversation_id = $1 AND deleted_at IS NULL "
        "  AND (server_timestamp, id) < ($2::timestamptz, $3::uuid) "
        "ORDER BY server_timestamp DESC, id DESC LIMIT $4",
        conversationId, timestamp, messageId, limit);
  } else {
    result = txn.exec_params(
        "SELECT id, conversation_id, user_id, "
        "       ciphertext AS ciphertext_hex, nonce, "
        "       encryption_version, client_timestamp::text, "
        "       server_timestamp::text "
        "FROM messages "
        "WHERE conversation_id = $1 AND deleted_at IS NULL "
        "ORDER BY server_timestamp DESC, id DESC LIMIT $2",
        conversationId, limit);
  }

  MessagePage page;
  for (const auto& row : result) {
    domain::Message m;
    m.id = row["id"].as<std::string>();
    m.conversation_id = row["conversation_id"].as<std::string>();
    if (!row["user_id"].is_null()) m.user_id = row["user_id"].as<std::string>();
    m.ciphertext = fromHex(row["ciphertext_hex"].as<std::string>());
    m.nonce = row["nonce"].as<std::string>();
    m.encryption_version = row["encryption_version"].as<int16_t>();
    m.client_timestamp = row["client_timestamp"].as<std::string>();
    m.server_timestamp = row["server_timestamp"].as<std::string>();
    m.attachments = fetchAttachments(txn, m.id);
    page.messages.push_back(std::move(m));
  }

  txn.commit();

  // Only offer a next page if we got a full page — a short page means we hit
  // the end of history.
  if (static_cast<int>(page.messages.size()) == limit) {
    page.nextCursor = page.messages.back().server_timestamp + "|" +
                      page.messages.back().id;
  }

  return page;
}

}  // namespace messaging::repositories
