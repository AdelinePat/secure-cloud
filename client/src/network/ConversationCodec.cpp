#include "network/ConversationCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <stdexcept>

namespace network {

namespace {

QJsonObject parseObject(const QByteArray& json) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &error);

  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw std::runtime_error("Invalid conversation JSON");
  }

  return document.object();
}

QString optionalString(const QJsonObject& object, const char* key) {
  const QJsonValue value = object.value(key);
  return value.isString() ? value.toString() : QString{};
}

QStringList stringArray(const QJsonObject& object, const char* key) {
  const QJsonValue value = object.value(key);
  if (!value.isArray()) return {};

  QStringList values;
  for (const QJsonValue& item : value.toArray()) {
    if (item.isString()) values.append(item.toString());
  }
  return values;
}

dto::ConversationDetails parseDetailsObject(const QJsonObject& object) {
  dto::ConversationDetails details;
  details.id = object.value("conversation_id").toString();
  details.type = object.value("type").toString();
  details.name = optionalString(object, "name");
  details.createdAt = object.value("created_at").toString();
  details.creatorId = optionalString(object, "creator_id");

  if (details.id.isEmpty()) {
    throw std::runtime_error("Conversation is missing conversation_id");
  }

  const QJsonValue participants = object.value("participants");
  if (participants.isArray()) {
    for (const QJsonValue& item : participants.toArray()) {
      if (!item.isObject()) continue;

      const QJsonObject participant = item.toObject();
      details.participants.append(dto::ConversationParticipant{
          participant.value("user_id").toString(),
          participant.value("role").toString(),
          participant.value("joined_at").toString()});
    }
  }

  return details;
}

}  // namespace

QString ConversationCodec::serializeCreate(
    const dto::CreateConversationRequest& request) {
  QJsonObject object;
  object["type"] = request.type;

  QJsonArray participants;
  for (const QString& participantId : request.participantIds) {
    participants.append(participantId);
  }
  object["participant_ids"] = participants;

  if (!request.name.isEmpty()) {
    object["name"] = request.name;
  } else {
    object["name"] = QJsonValue::Null;
  }

  return QString::fromUtf8(
      QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QVector<dto::ConversationSummary> ConversationCodec::parseList(
    const QByteArray& json) {
  const QJsonObject root = parseObject(json);
  const QJsonValue conversations = root.value("conversations");

  if (!conversations.isArray()) {
    throw std::runtime_error("Missing conversations array");
  }

  QVector<dto::ConversationSummary> result;
  for (const QJsonValue& item : conversations.toArray()) {
    if (!item.isObject()) continue;

    const QJsonObject object = item.toObject();

    dto::ConversationSummary summary;
    summary.id = object.value("conversation_id").toString();
    summary.type = object.value("type").toString();
    summary.name = optionalString(object, "name");
    summary.participantIds = stringArray(object, "participant_ids");
    summary.lastMessageAt = optionalString(object, "last_message_at");
    summary.unreadCount = object.value("unread_count").toInt();

    if (!summary.id.isEmpty()) result.append(summary);
  }

  return result;
}

dto::ConversationDetails ConversationCodec::parseDetails(
    const QByteArray& json) {
  return parseDetailsObject(parseObject(json));
}

}  // namespace network
