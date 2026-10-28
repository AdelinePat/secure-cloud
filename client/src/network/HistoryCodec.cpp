#include "network/HistoryCodec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <stdexcept>

#include "crypto/MessageSerializer.hpp"

namespace network {

namespace {

dto::MessageAttachment parseAttachment(const QJsonObject& object) {
  dto::MessageAttachment attachment;
  attachment.fileId = object.value("file_id").toString();
  attachment.sizeBytes =
      static_cast<qint64>(object.value("size_bytes").toDouble());
  attachment.checksumSha256 = object.value("checksum_sha256").toString();
  return attachment;
}

dto::ReceivedMessage parseMessage(const QJsonObject& object) {
  dto::ReceivedMessage message;

  message.messageId = object.value("message_id").toString();
  message.userId = object.value("userId").toString();
  if (message.userId.isEmpty()) {
    message.userId = object.value("user_id").toString();
  }

  const QString nonce = object.value("nonce").toString();
  const QString ciphertext = object.value("ciphertext").toString();

  try {
    message.encryptedMessage.nonce =
        crypto::MessageSerializer::base64ToNonce(nonce.toStdString());
    message.encryptedMessage.ciphertext =
        crypto::MessageSerializer::base64ToCiphertext(
            ciphertext.toStdString());
  } catch (const std::runtime_error&) {
    throw std::runtime_error("Invalid encrypted message payload");
  }

  message.encryptionVersion = object.value("encryption_version").toInt(1);
  message.serverTimestamp = object.value("server_timestamp").toString();

  const QJsonValue attachments = object.value("attachments");
  if (attachments.isArray()) {
    for (const QJsonValue& item : attachments.toArray()) {
      if (item.isObject()) {
        message.attachments.append(parseAttachment(item.toObject()));
      }
    }
  }

  if (message.messageId.isEmpty()) {
    throw std::runtime_error("Message is missing message_id");
  }

  return message;
}

}  // namespace

dto::HistoryPage HistoryCodec::parse(const QByteArray& json) {
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &error);

  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    throw std::runtime_error("Invalid history JSON");
  }

  const QJsonObject root = document.object();
  const QJsonValue messages = root.value("messages");

  if (!messages.isArray()) {
    throw std::runtime_error("Missing messages array");
  }

  dto::HistoryPage page;
  for (const QJsonValue& item : messages.toArray()) {
    if (item.isObject()) page.messages.append(parseMessage(item.toObject()));
  }

  const QJsonValue nextCursor = root.value("next_cursor");
  if (nextCursor.isString() && !nextCursor.toString().isEmpty()) {
    page.nextCursor = nextCursor.toString();
    page.hasNext = true;
  }

  return page;
}

}  // namespace network
