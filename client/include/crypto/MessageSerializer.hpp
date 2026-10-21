#pragma once

#include <string>

#include "crypto/CryptoTypes.hpp"

namespace crypto {

class MessageSerializer {
 public:
  static std::string nonceToBase64(const Nonce& nonce);

  static Nonce base64ToNonce(const std::string& encodedNonce);

  static std::string ciphertextToBase64(const Ciphertext& ciphertext);

  static Ciphertext base64ToCiphertext(const std::string& encodedCiphertext);
};

}  // namespace crypto