#pragma once

#include <QString>

#include "crypto/CryptoTypes.hpp"

namespace crypto {

class IdentityKeyStore {
 public:
  // Checks whether an identity is already stored locally.
  static bool exists(const QString& userId);

  // Saves an encrypted identity key pair.
  static void save(const QString& userId, const KeyPair& keyPair,
                   const QString& passphrase);

  // Loads and decrypts an existing identity key pair.
  static KeyPair load(const QString& userId, const QString& passphrase);
};

}  // namespace crypto