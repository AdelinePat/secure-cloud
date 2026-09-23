#include <sodium.h>

#include <string>

#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"
#include "network/MessageCodec.hpp"

int main() {
  if (sodium_init() < 0) {
    return 1;
  }

  // 1. Générer les clés Alice et Bob
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

  std::string serializedMessage =
      network::MessageCodec::serialize(encryptedMessage);

  crypto::EncryptedMessage receivedMessage =
      network::MessageCodec::deserialize(serializedMessage);

  std::string decryptedMessage =
      crypto::MessageCrypto::decrypt(receivedMessage, bobSessionKeys.rx);

  if (decryptedMessage != message) {
    return 1;
  }

  return 0;
}