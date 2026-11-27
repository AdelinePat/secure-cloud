#include <sodium.h>

#include <stdexcept>
#include <string>

#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"
#include "network/MessageCodec.hpp"
#include "network/GroupMessage.hpp"

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

  const auto charlieKeys = crypto::KeyManager::generateKeyPair();
  const auto aliceToCharlie = crypto::KeyManager::deriveClientSessionKeys(
      aliceKeys.publicKey, aliceKeys.privateKey, charlieKeys.publicKey);
  const auto charlieFromAlice = crypto::KeyManager::deriveServerSessionKeys(
      charlieKeys.publicKey, charlieKeys.privateKey, aliceKeys.publicKey);
  const QByteArray groupText = "Hello Bob and Charlie!";
  const auto outgoing = network::GroupMessage::encrypt(
      groupText, "group", "message", aliceKeys.privateKey,
      {{"bob", aliceSessionKeys}, {"charlie", aliceToCharlie}});
  const QByteArray envelope(
      reinterpret_cast<const char*>(outgoing.ciphertext.data()), outgoing.ciphertext.size());
  const QByteArray nonce(
      reinterpret_cast<const char*>(outgoing.nonce.data()), outgoing.nonce.size());
  if (envelope.contains(groupText) ||
      network::GroupMessage::decrypt(envelope, nonce, "group", "message", "alice",
                                     "bob", bobKeys.privateKey,
                                     {{"alice", bobSessionKeys}}) != groupText ||
      network::GroupMessage::decrypt(envelope, nonce, "group", "message", "alice",
                                     "charlie", charlieKeys.privateKey,
                                     {{"alice", charlieFromAlice}}) != groupText ||
      network::GroupMessage::decrypt(envelope, nonce, "group", "message", "alice",
                                     "alice", aliceKeys.privateKey, {}) != groupText) {
    return 1;
  }

  bool rejectsWrongMessage = false;
  try {
    network::GroupMessage::decrypt(envelope, nonce, "group", "other-message",
                                   "alice", "bob", bobKeys.privateKey,
                                   {{"alice", bobSessionKeys}});
  } catch (const std::runtime_error&) {
    rejectsWrongMessage = true;
  }
  bool rejectsOutsider = false;
  try {
    network::GroupMessage::decrypt(envelope, nonce, "group", "message",
                                   "alice", "outsider", bobKeys.privateKey, {});
  } catch (const std::runtime_error&) {
    rejectsOutsider = true;
  }
  return rejectsWrongMessage && rejectsOutsider ? 0 : 1;
}