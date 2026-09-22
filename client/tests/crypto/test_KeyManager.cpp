#include <sodium.h>

#include "crypto/KeyManager.hpp"

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

  if (aliceSessionKeys.tx != bobSessionKeys.rx) {
    return 1;
  }

  if (aliceSessionKeys.rx != bobSessionKeys.tx) {
    return 1;
  }

  return 0;
}