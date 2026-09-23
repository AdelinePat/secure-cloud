#include "network/MessageCodec.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <stdexcept>

#include "crypto/MessageSerializer.hpp"

namespace network {

std::string MessageCodec::serialize(
    const crypto::EncryptedMessage& encryptedMessage) {
  std::string encodedNonce =
      crypto::MessageSerializer::nonceToBase64(encryptedMessage.nonce);

  std::string encodedCiphertext = crypto::MessageSerializer::ciphertextToBase64(
      encryptedMessage.ciphertext);

  QJsonObject jsonObject;

  QString nonceValue = QString::fromStdString(encodedNonce);

  QString ciphertextValue = QString::fromStdString(encodedCiphertext);

  jsonObject["nonce"] = nonceValue;
  jsonObject["ciphertext"] = ciphertextValue;

  QJsonDocument jsonDocument(jsonObject);

  QByteArray jsonData = jsonDocument.toJson(QJsonDocument::Compact);

  return jsonData.toStdString();
}

crypto::EncryptedMessage MessageCodec::deserialize(const std::string& json) {
  crypto::EncryptedMessage encryptedMessage;

  QByteArray jsonData = QByteArray::fromStdString(json);

  QJsonParseError parseError;

  QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonData, &parseError);

  if (parseError.error != QJsonParseError::NoError) {
    throw std::runtime_error("Invalid JSON");
  }

  if (!jsonDocument.isObject()) {
    throw std::runtime_error("JSON root must be an object");
  }

  QJsonObject jsonObject = jsonDocument.object();

  if (!jsonObject.contains("nonce") || !jsonObject["nonce"].isString() ||
      !jsonObject.contains("ciphertext") ||
      !jsonObject["ciphertext"].isString()) {
    throw std::runtime_error("Invalid encrypted message JSON");
  }

  QString nonceValue = jsonObject["nonce"].toString();

  QString ciphertextValue = jsonObject["ciphertext"].toString();

  std::string encodedNonce = nonceValue.toStdString();

  std::string encodedCiphertext = ciphertextValue.toStdString();

  encryptedMessage.nonce =
      crypto::MessageSerializer::base64ToNonce(encodedNonce);

  encryptedMessage.ciphertext =
      crypto::MessageSerializer::base64ToCiphertext(encodedCiphertext);

  return encryptedMessage;
}
}  // namespace network