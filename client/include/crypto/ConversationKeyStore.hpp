#pragma once

#include <QString>

#include "crypto/CryptoTypes.hpp"

namespace crypto {

// MVP key derivation for a conversation.
//
// Both participants derive the same key from the conversation id and a shared
// passphrase. This keeps the encryption/decryption boundary in the client, but
// it is NOT a substitute for a real key agreement: production should use
// crypto_kx (1:1) or an MLS implementation (groups). See
// _Notes/project_encryption_notes.md.
class ConversationKeyStore {
 public:
  static SessionKey derive(const QString& conversationId,
                           const QString& passphrase);
};

}  // namespace crypto
