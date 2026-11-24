
#include <sodium.h>

#include <iostream>
#include <stdexcept>
#include <string>

#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"

namespace {

// Verify that invalid encrypted messages are rejected.
bool expectDecryptionFailure(
    const crypto::EncryptedMessage& encryptedMessage,
    const crypto::SessionKey& key,
    const std::string& expectedError,
    const std::string& testName) {
  try {
    crypto::MessageCrypto::decrypt(encryptedMessage, key);

    std::cerr << "[FAIL] " << testName
              << ": decryption unexpectedly succeeded\n";

    return false;

  } catch (const std::runtime_error& error) {
    if (expectedError != error.what()) {
      std::cerr << "[FAIL] " << testName
                << ": unexpected error: " << error.what() << '\n';

      return false;
    }
  }

  std::cout << "[PASS] " << testName << '\n';

  return true;
}

}  // namespace

int main() {
  if (sodium_init() < 0) {
    return 1;
  }

  const auto aliceKeys = crypto::KeyManager::generateKeyPair();
  const auto bobKeys = crypto::KeyManager::generateKeyPair();

  const auto aliceSessionKeys =
      crypto::KeyManager::deriveClientSessionKeys(
          aliceKeys.publicKey,
          aliceKeys.privateKey,
          bobKeys.publicKey);

  const auto bobSessionKeys =
      crypto::KeyManager::deriveServerSessionKeys(
          bobKeys.publicKey,
          bobKeys.privateKey,
          aliceKeys.publicKey);

  const std::string message = "Hello Bob!";

  const auto encryptedMessage =
      crypto::MessageCrypto::encrypt(
          message, aliceSessionKeys.tx);

  bool success = true;

  // 1. Valid encryption and decryption.
  const auto decryptedMessage =
      crypto::MessageCrypto::decrypt(
          encryptedMessage, bobSessionKeys.rx);

  if (decryptedMessage != message) {
    std::cerr << "[FAIL] Encryption/decryption round trip\n";
    success = false;
  } else {
    std::cout << "[PASS] Encryption/decryption round trip\n";
  }

  // 2. Reject a modified authentication tag.
  auto tamperedMac = encryptedMessage;
  tamperedMac.ciphertext[0] ^= 0x01;

  success &= expectDecryptionFailure(
      tamperedMac,
      bobSessionKeys.rx,
      "Failed to decrypt message",
      "Modified MAC");

  // 3. Reject modified encrypted message content.
  auto tamperedCiphertext = encryptedMessage;
  tamperedCiphertext.ciphertext[crypto_secretbox_MACBYTES] ^= 0x01;

  success &= expectDecryptionFailure(
      tamperedCiphertext,
      bobSessionKeys.rx,
      "Failed to decrypt message",
      "Modified ciphertext");

  // 4. Reject a modified nonce.
  auto tamperedNonce = encryptedMessage;
  tamperedNonce.nonce[0] ^= 0x01;

  success &= expectDecryptionFailure(
      tamperedNonce,
      bobSessionKeys.rx,
      "Failed to decrypt message",
      "Modified nonce");

  // 5. Reject an incorrect decryption key.
  auto wrongKey = bobSessionKeys.rx;
  wrongKey[0] ^= 0x01;

  success &= expectDecryptionFailure(
      encryptedMessage,
      wrongKey,
      "Failed to decrypt message",
      "Incorrect session key");

  // 6. Reject a truncated ciphertext.
  auto truncatedMessage = encryptedMessage;
  truncatedMessage.ciphertext.pop_back();

  success &= expectDecryptionFailure(
      truncatedMessage,
      bobSessionKeys.rx,
      "Failed to decrypt message",
      "Truncated ciphertext");

  // 7. Reject ciphertext shorter than the MAC.
  auto shortMessage = encryptedMessage;
  shortMessage.ciphertext.resize(
      crypto_secretbox_MACBYTES - 1);

  success &= expectDecryptionFailure(
      shortMessage,
      bobSessionKeys.rx,
      "Ciphertext is too short to contain a valid MAC",
      "Ciphertext shorter than MAC");

  // 8. Reject an empty ciphertext.
  auto emptyCiphertext = encryptedMessage;
  emptyCiphertext.ciphertext.clear();

  success &= expectDecryptionFailure(
      emptyCiphertext,
      bobSessionKeys.rx,
      "Ciphertext is too short to contain a valid MAC",
      "Empty ciphertext");

  // 9. Verify encryption of an empty plaintext.
  const auto encryptedEmpty =
      crypto::MessageCrypto::encrypt(
          "", aliceSessionKeys.tx);

  const auto decryptedEmpty =
      crypto::MessageCrypto::decrypt(
          encryptedEmpty, bobSessionKeys.rx);

  if (!decryptedEmpty.empty() ||
      encryptedEmpty.ciphertext.size() !=
          crypto_secretbox_MACBYTES) {
    std::cerr << "[FAIL] Empty plaintext\n";
    success = false;
  } else {
    std::cout << "[PASS] Empty plaintext\n";
  }

  // 10. Verify nonce freshness across two encryptions.
  const auto secondEncryption =
      crypto::MessageCrypto::encrypt(
          message, aliceSessionKeys.tx);

  if (encryptedMessage.nonce == secondEncryption.nonce ||
      encryptedMessage.ciphertext ==
          secondEncryption.ciphertext) {
    std::cerr << "[FAIL] Nonce freshness\n";
    success = false;
  } else {
    std::cout << "[PASS] Nonce freshness\n";
  }

  // 11. Verify communication in the opposite direction.
  const std::string reply = "Hello Alice!";

  const auto encryptedReply =
      crypto::MessageCrypto::encrypt(
          reply, bobSessionKeys.tx);

  const auto decryptedReply =
      crypto::MessageCrypto::decrypt(
          encryptedReply, aliceSessionKeys.rx);

  if (decryptedReply != reply) {
    std::cerr << "[FAIL] Reverse communication\n";
    success = false;
  } else {
    std::cout << "[PASS] Reverse communication\n";
  }

  return success ? 0 : 1;
}
