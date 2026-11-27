#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

#include "crypto/CryptoTypes.hpp"

namespace network {

inline constexpr int kGroupTextMessageVersion = 4;

class GroupMessage {
 public:
  static crypto::EncryptedMessage encrypt(
      const QByteArray& text, const QString& conversationId,
      const QString& messageId, const crypto::PrivateKey& localKey,
      const QHash<QString, crypto::SessionKeys>& peers);

  static QByteArray decrypt(
      const QByteArray& envelope, const QByteArray& nonce,
      const QString& conversationId, const QString& messageId,
      const QString& senderId, const QString& localId,
      const crypto::PrivateKey& localKey,
      const QHash<QString, crypto::SessionKeys>& peers);
};

}  // namespace network
