#pragma once

#include <string>

#include "crypto/CryptoTypes.hpp"

namespace crypto {

class MessageSerializer {
 public:
  static std::string nonceToBase64(const Nonce& nonce);

  static Nonce base64ToNonce(const std::string& encodedNonce);
};

}  // namespace crypto