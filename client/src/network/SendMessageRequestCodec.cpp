#include "network/SendMessageRequestCodec.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QJsonArray>

#include "crypto/MessageSerializer.hpp"

namespace network {

std::string SendMessageRequestCodec::serialize(
    const dto::SendMessageRequest& request) {
  QJsonObject jsonObject;
  jsonObject["message_id"] = QString::fromStdString(request.messageId);
  jsonObject["nonce"] = QString::fromStdString(
      crypto::MessageSerializer::nonceToBase64(request.encryptedMessage.nonce));
  jsonObject["ciphertext"] = QString::fromStdString(
      crypto::MessageSerializer::ciphertextToBase64(
          request.encryptedMessage.ciphertext));
  jsonObject["encryption_version"] = request.encryptionVersion;
  jsonObject["client_timestamp"] = QString::fromStdString(request.clientTimestamp);
  jsonObject["attachments"] = QJsonArray();
  QJsonDocument jsonDocument(jsonObject);
  QByteArray jsonData = jsonDocument.toJson(QJsonDocument::Compact);
  return jsonData.toStdString();
}

}  // namespace network