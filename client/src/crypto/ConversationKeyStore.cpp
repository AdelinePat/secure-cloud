#include "crypto/ConversationKeyStore.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <stdexcept>

namespace crypto {

SessionKey ConversationKeyStore::derive(const QString& conversationId,
                                        const QString& passphrase) {
  if (conversationId.isEmpty()) {
    throw std::runtime_error("Cannot derive a key without a conversation id");
  }
  if (passphrase.isEmpty()) {
    throw std::runtime_error("Cannot derive a key without a passphrase");
  }

  const QByteArray passphraseBytes = passphrase.toUtf8();
  const QByteArray conversationBytes = conversationId.toUtf8();

  // crypto_generichash accepts keys up to 64 bytes; hash longer passphrases
  // first so arbitrary user input is accepted.
  std::array<unsigned char, crypto_generichash_KEYBYTES> key{};
  if (passphraseBytes.size() > static_cast<int>(key.size())) {
    crypto_generichash(key.data(), key.size(),
                       reinterpret_cast<const unsigned char*>(
                           passphraseBytes.constData()),
                       passphraseBytes.size(), nullptr, 0);
  } else {
    std::copy(passphraseBytes.constData(),
              passphraseBytes.constData() + passphraseBytes.size(),
              key.begin());
  }

  SessionKey sessionKey{};
  if (crypto_generichash(
          sessionKey.data(), sessionKey.size(),
          reinterpret_cast<const unsigned char*>(conversationBytes.constData()),
          conversationBytes.size(), key.data(), key.size()) != 0) {
    throw std::runtime_error("Failed to derive the conversation key");
  }

  return sessionKey;
}

}  // namespace crypto
