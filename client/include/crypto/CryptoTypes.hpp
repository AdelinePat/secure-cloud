#pragma once

#include <sodium.h>

#include <array>
#include <vector>

namespace crypto {
using PublicKey = std::array<unsigned char, crypto_kx_PUBLICKEYBYTES>;

using PrivateKey = std::array<unsigned char, crypto_kx_SECRETKEYBYTES>;

using SessionKey = std::array<unsigned char, crypto_kx_SESSIONKEYBYTES>;

using Nonce = std::array<unsigned char, crypto_secretbox_NONCEBYTES>;

using Ciphertext = std::vector<unsigned char>;

struct KeyPair {
  PublicKey publicKey;
  PrivateKey privateKey;
};

struct SessionKeys {
  SessionKey rx;  // Receiving key
  SessionKey tx;  // Transmitting key
};

struct EncryptedMessage {
  Nonce nonce;
  Ciphertext ciphertext;
};

}  // namespace crypto