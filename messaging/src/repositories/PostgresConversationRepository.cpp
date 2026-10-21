#include "messaging/repositories/PostgresConversationRepository.hpp"

namespace messaging::repositories {

PostgresConversationRepository::PostgresConversationRepository(
    pqxx::connection& conn)
    : conn_(conn) {}

std::vector<domain::Participant>
PostgresConversationRepository::fetchParticipants(
    pqxx::work& txn, const std::string& conversationId) {
  auto rows = txn.exec_params(
      "SELECT user_id, role, joined_at::text FROM conversation_participants "
      "WHERE conversation_id = $1 ORDER BY joined_at",
      conversationId);

  std::vector<domain::Participant> participants;
  for (const auto& row : rows) {
    participants.push_back(domain::Participant{
        row["user_id"].as<std::string>(), row["role"].as<std::string>(),
        row["joined_at"].as<std::string>()});
  }
  return participants;
}

domain::Conversation PostgresConversationRepository::create(
    const domain::Conversation& conversation) {
  pqxx::work txn(conn_);

  auto convRow = txn.exec_params(
      "INSERT INTO conversations (creator_id, type, name) "
      "VALUES ($1, $2, $3) "
      "RETURNING id, creator_id, type, name, created_at::text, updated_at::text",
      conversation.creator_id, conversation.type, conversation.name);

  domain::Conversation stored;
  stored.id = convRow[0]["id"].as<std::string>();
  if (!convRow[0]["creator_id"].is_null())
    stored.creator_id = convRow[0]["creator_id"].as<std::string>();
  stored.type = convRow[0]["type"].as<std::string>();
  if (!convRow[0]["name"].is_null())
    stored.name = convRow[0]["name"].as<std::string>();
  stored.created_at = convRow[0]["created_at"].as<std::string>();
  stored.updated_at = convRow[0]["updated_at"].as<std::string>();

  for (const auto& p : conversation.participants) {
    auto pRow = txn.exec_params(
        "INSERT INTO conversation_participants (conversation_id, user_id, role) "
        "VALUES ($1, $2, $3) RETURNING joined_at::text",
        stored.id, p.user_id, p.role);

    stored.participants.push_back(domain::Participant{
        p.user_id, p.role, pRow[0]["joined_at"].as<std::string>()});
  }

  txn.commit();  // participants insert atomically with the conversation, or not at all
  return stored;
}

std::optional<domain::Conversation> PostgresConversationRepository::findById(
    const std::string& id) {
  pqxx::work txn(conn_);

  auto convRows = txn.exec_params(
      "SELECT id, creator_id, type, name, created_at::text, updated_at::text "
      "FROM conversations WHERE id = $1",
      id);

  if (convRows.empty()) {
    txn.commit();
    return std::nullopt;
  }

  domain::Conversation conv;
  conv.id = convRows[0]["id"].as<std::string>();
  if (!convRows[0]["creator_id"].is_null())
    conv.creator_id = convRows[0]["creator_id"].as<std::string>();
  conv.type = convRows[0]["type"].as<std::string>();
  if (!convRows[0]["name"].is_null())
    conv.name = convRows[0]["name"].as<std::string>();
  conv.created_at = convRows[0]["created_at"].as<std::string>();
  conv.updated_at = convRows[0]["updated_at"].as<std::string>();
  conv.participants = fetchParticipants(txn, id);

  txn.commit();
  return conv;
}

std::vector<domain::Conversation> PostgresConversationRepository::listForUser(
    const std::string& userId) {
  pqxx::work txn(conn_);

  // NOTE: one extra query per conversation to fetch its participants (N+1).
  // Fine at MVP scale; if this shows up in profiling later, replace with a
  // single query joining conversation_participants and grouping client-side.
  auto convRows = txn.exec_params(
      "SELECT c.id, c.creator_id, c.type, c.name, c.created_at::text, c.updated_at::text "
      "FROM conversations c "
      "JOIN conversation_participants cp ON cp.conversation_id = c.id "
      "WHERE cp.user_id = $1 "
      "ORDER BY c.updated_at DESC",
      userId);

  std::vector<domain::Conversation> result;
  for (const auto& row : convRows) {
    domain::Conversation conv;
    conv.id = row["id"].as<std::string>();
    if (!row["creator_id"].is_null())
      conv.creator_id = row["creator_id"].as<std::string>();
    conv.type = row["type"].as<std::string>();
    if (!row["name"].is_null()) conv.name = row["name"].as<std::string>();
    conv.created_at = row["created_at"].as<std::string>();
    conv.updated_at = row["updated_at"].as<std::string>();
    conv.participants = fetchParticipants(txn, conv.id);
    result.push_back(std::move(conv));
  }

  txn.commit();
  return result;
}

bool PostgresConversationRepository::remove(const std::string& id) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params("DELETE FROM conversations WHERE id = $1", id);
  txn.commit();
  return result.affected_rows() > 0;
}

bool PostgresConversationRepository::addParticipant(
    const std::string& conversationId, const std::string& userId,
    const std::string& role) {
  pqxx::work txn(conn_);
  txn.exec_params(
      "INSERT INTO conversation_participants (conversation_id, user_id, role) "
      "VALUES ($1, $2, $3) "
      "ON CONFLICT (conversation_id, user_id) DO NOTHING",
      conversationId, userId, role);
  txn.commit();
  return true;
}

bool PostgresConversationRepository::removeParticipant(
    const std::string& conversationId, const std::string& userId) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "DELETE FROM conversation_participants "
      "WHERE conversation_id = $1 AND user_id = $2",
      conversationId, userId);
  txn.commit();
  return result.affected_rows() > 0;
}

std::optional<std::string> PostgresConversationRepository::findRole(
    const std::string& conversationId, const std::string& userId) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "SELECT role FROM conversation_participants "
      "WHERE conversation_id = $1 AND user_id = $2",
      conversationId, userId);
  txn.commit();

  if (result.empty()) return std::nullopt;
  return result[0]["role"].as<std::string>();
}

}  // namespace messaging::repositories
