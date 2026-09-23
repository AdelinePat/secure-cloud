#include <sodium.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "crypto/MessageSerializer.hpp"
#include "network/SendMessageRequestCodec.hpp"
#include "network/dto/SendMessageRequest.hpp"

int main() {
  if (sodium_init() < 0) {
    return 1;
  }

  network::dto::SendMessageRequest request;

  request.messageId = "9f1c2e3a-test";
  request.encryptionVersion = 1;
  request.clientTimestamp = "2026-09-23T14:32:00Z";

  randombytes_buf(request.encryptedMessage.nonce.data(),
                  request.encryptedMessage.nonce.size());

  request.encryptedMessage.ciphertext.resize(100);
  randombytes_buf(request.encryptedMessage.ciphertext.data(),
                  request.encryptedMessage.ciphertext.size());

  std::string serializedRequest =
      network::SendMessageRequestCodec::serialize(request);

  QByteArray jsonData = QByteArray::fromStdString(serializedRequest);
  QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonData);
  QJsonObject jsonObject = jsonDocument.object();

  if (jsonObject["message_id"].toString() !=
          QString::fromStdString(request.messageId) ||
      jsonObject["encryption_version"].toInt() != request.encryptionVersion ||
      jsonObject["client_timestamp"].toString() !=
          QString::fromStdString(request.clientTimestamp) ||
      jsonObject["nonce"].toString() !=
          QString::fromStdString(crypto::MessageSerializer::nonceToBase64(
              request.encryptedMessage.nonce)) ||
      jsonObject["ciphertext"].toString() !=
          QString::fromStdString(crypto::MessageSerializer::ciphertextToBase64(
              request.encryptedMessage.ciphertext)) ||
      !jsonObject.contains("attachments") ||
      !jsonObject["attachments"].isArray() ||
      !jsonObject["attachments"].toArray().isEmpty()) {
    return 1;
  }

  return 0;
}