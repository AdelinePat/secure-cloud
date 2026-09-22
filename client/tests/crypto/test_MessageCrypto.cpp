#include <sodium.h>

#include <stdexcept>

#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"

int main() {
  if (sodium_init() < 0) {
    return 1;
  }

  crypto::KeyPair aliceKeys = crypto::KeyManager::generateKeyPair();

  crypto::KeyPair bobKeys = crypto::KeyManager::generateKeyPair();

  crypto::SessionKeys aliceSessionKeys =
      crypto::KeyManager::deriveClientSessionKeys(
          aliceKeys.publicKey, aliceKeys.privateKey, bobKeys.publicKey);

  crypto::SessionKeys bobSessionKeys =
      crypto::KeyManager::deriveServerSessionKeys(
          bobKeys.publicKey, bobKeys.privateKey, aliceKeys.publicKey);

  std::string message = "Hello Bob!";

  crypto::EncryptedMessage encryptedMessage =
      crypto::MessageCrypto::encrypt(message, aliceSessionKeys.tx);

  std::string decryptedMessage =
      crypto::MessageCrypto::decrypt(encryptedMessage, bobSessionKeys.rx);

  if (decryptedMessage != message) {
    return 1;
  }

  crypto::EncryptedMessage tamperedMessage =
      crypto::MessageCrypto::encrypt(message, aliceSessionKeys.tx);

  tamperedMessage.ciphertext[0] ^= 0x01;

  try {
    crypto::MessageCrypto::decrypt(tamperedMessage, bobSessionKeys.rx);
    return 1;
  } catch (const std::runtime_error& error) {
    try {
      crypto::MessageCrypto::decrypt(tamperedMessage, bobSessionKeys.rx);
      return 1;
    } catch (const std::runtime_error& error) {
      try {
        crypto::MessageCrypto::decrypt(tamperedMessage, bobSessionKeys.rx);
        return 1;
      } catch (const std::runtime_error& error) {
        if (std::string(error.what()) != "Failed to decrypt message") {
          return 1;
        }
      }
    }
  }

  crypto::SessionKey wrongKey;
  randombytes_buf(wrongKey.data(), wrongKey.size());

  try {
    crypto::MessageCrypto::decrypt(encryptedMessage, wrongKey);
    return 1;
  } catch (const std::runtime_error& error) {
    if (std::string(error.what()) != "Failed to decrypt message") {
      return 1;
    }
  }

  return 0;
}