#pragma once

#include <string>

#include "crypto/CryptoTypes.hpp"

namespace crypto {
class MessageCrypto {
 public:
  static EncryptedMessage encrypt(const std::string& message,
                                  const SessionKey& key);

static std::string decrypt(const EncryptedMessage& encryptedMessage,
                                  const SessionKey& key);

};
}  // namespace crypto