#include "crypto/KeyManager.hpp"

#include <sodium.h>

#include <stdexcept>

namespace crypto {

KeyPair KeyManager::generateKeyPair() {
  KeyPair keyPair;

  crypto_kx_keypair(keyPair.publicKey.data(), keyPair.privateKey.data());

  return keyPair;
}

SessionKeys KeyManager::deriveClientSessionKeys(
    const PublicKey& ownPublicKey, const PrivateKey& ownPrivateKey,
    const PublicKey& peerPublicKey) {
  SessionKeys sessionKeys;

  int result = crypto_kx_client_session_keys(
      sessionKeys.rx.data(), sessionKeys.tx.data(), ownPublicKey.data(),
      ownPrivateKey.data(), peerPublicKey.data());

  if (result != 0) {
    throw std::runtime_error("Failed to derive client session keys");
  }

  return sessionKeys;
}

SessionKeys KeyManager::deriveServerSessionKeys(
    const PublicKey& ownPublicKey, const PrivateKey& ownPrivateKey,
    const PublicKey& peerPublicKey) {
  SessionKeys sessionKeys;

  int result = crypto_kx_server_session_keys(
      sessionKeys.rx.data(), sessionKeys.tx.data(), ownPublicKey.data(),
      ownPrivateKey.data(), peerPublicKey.data());

  if (result != 0) {
    throw std::runtime_error("Failed to derive server session keys");
  }
  return sessionKeys;
}
}  // namespace crypto