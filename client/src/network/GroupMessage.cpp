#include "network/GroupMessage.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <stdexcept>

#include "crypto/MessageCrypto.hpp"

namespace network {
namespace {

struct MessageKey {
  crypto::SessionKey bytes{};
  ~MessageKey() { sodium_memzero(bytes.data(), bytes.size()); }
};

std::string context(const QString& conversationId, const QString& messageId,
                    bool ownCopy) {
  return (QStringList{ownCopy ? "securechat-group-v4-own" : "securechat-group-v4-peer",
                      conversationId, messageId}).join('/').toStdString();
}

void deriveKey(MessageKey& key, const std::string& info,
               const unsigned char* secret, std::size_t size) {
  if (crypto_generichash(
          key.bytes.data(), key.bytes.size(),
          reinterpret_cast<const unsigned char*>(info.data()), info.size(),
          secret, size) != 0) {
    throw std::runtime_error("Cannot derive group message key");
  }
}

QByteArray encode(const unsigned char* data, std::size_t size) {
  return QByteArray(reinterpret_cast<const char*>(data),
                    static_cast<qsizetype>(size)).toBase64();
}

crypto::EncryptedMessage payload(const QByteArray& nonce,
                                 const QByteArray& ciphertext) {
  if (nonce.size() != crypto_secretbox_NONCEBYTES ||
      ciphertext.size() < crypto_secretbox_MACBYTES) {
    throw std::runtime_error("Invalid group ciphertext");
  }
  crypto::EncryptedMessage result;
  std::copy(nonce.cbegin(), nonce.cend(), result.nonce.begin());
  result.ciphertext.assign(ciphertext.cbegin(), ciphertext.cend());
  return result;
}

}  // namespace

crypto::EncryptedMessage GroupMessage::encrypt(
    const QByteArray& text, const QString& conversationId,
    const QString& messageId, const crypto::PrivateKey& localKey,
    const QHash<QString, crypto::SessionKeys>& peers) {
  if (peers.size() != 2) {
    throw std::runtime_error("Two peer keys are required");
  }

  const auto peerContext = context(conversationId, messageId, false);
  QJsonObject recipients;
  for (auto peer = peers.cbegin(); peer != peers.cend(); ++peer) {
    MessageKey key;
    deriveKey(key, peerContext, peer->tx.data(), peer->tx.size());
    const auto encrypted = crypto::MessageCrypto::encrypt(text.toStdString(), key.bytes);
    recipients.insert(peer.key(), QJsonObject{
        {"nonce", QString::fromLatin1(encode(encrypted.nonce.data(), encrypted.nonce.size()))},
        {"ciphertext", QString::fromLatin1(
            encode(encrypted.ciphertext.data(), encrypted.ciphertext.size()))}});
  }

  MessageKey ownKey;
  deriveKey(ownKey, context(conversationId, messageId, true),
            localKey.data(), localKey.size());
  auto own = crypto::MessageCrypto::encrypt(text.toStdString(), ownKey.bytes);
  const auto json = QJsonDocument(QJsonObject{
      {"own", QString::fromLatin1(encode(own.ciphertext.data(), own.ciphertext.size()))},
      {"recipients", recipients}}).toJson(QJsonDocument::Compact);
  own.ciphertext.assign(json.cbegin(), json.cend());
  return own;
}

QByteArray GroupMessage::decrypt(
    const QByteArray& envelope, const QByteArray& nonce,
    const QString& conversationId, const QString& messageId,
    const QString& senderId, const QString& localId,
    const crypto::PrivateKey& localKey,
    const QHash<QString, crypto::SessionKeys>& peers) {
  const auto object = QJsonDocument::fromJson(envelope).object();
  constexpr auto strict = QByteArray::AbortOnBase64DecodingErrors;
  MessageKey key;
  crypto::EncryptedMessage encrypted;
  if (senderId == localId) {
    deriveKey(key, context(conversationId, messageId, true),
              localKey.data(), localKey.size());
    encrypted = payload(nonce, QByteArray::fromBase64(
        object.value("own").toString().toLatin1(), strict));
  } else {
    const auto sender = peers.constFind(senderId);
    if (sender == peers.cend()) {
      throw std::runtime_error("Unknown group sender");
    }
    const auto recipient = object.value("recipients").toObject().value(localId).toObject();
    deriveKey(key, context(conversationId, messageId, false),
              sender->rx.data(), sender->rx.size());
    encrypted = payload(
        QByteArray::fromBase64(recipient.value("nonce").toString().toLatin1(), strict),
        QByteArray::fromBase64(recipient.value("ciphertext").toString().toLatin1(), strict));
  }
  auto plaintext = crypto::MessageCrypto::decrypt(encrypted, key.bytes);
  const auto result = QByteArray::fromStdString(plaintext);
  sodium_memzero(plaintext.data(), plaintext.size());
  return result;
}

}  // namespace network
