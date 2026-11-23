
#include <sodium.h>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>

#include <cassert>
#include <iostream>
#include <stdexcept>

#include "crypto/IdentityKeyStore.hpp"
#include "crypto/KeyManager.hpp"

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  QStandardPaths::setTestModeEnabled(true);

  const QString userId =
      QUuid::createUuid().toString(QUuid::WithoutBraces);

  const QString passphrase = "test-passphrase-123";

  if (sodium_init() < 0) {
    return 1;
  }

  // Generate an identity.
  const crypto::KeyPair original =
      crypto::KeyManager::generateKeyPair();

  // Identity must not exist initially.
  assert(!crypto::IdentityKeyStore::exists(userId));

  // Save the identity.
  crypto::IdentityKeyStore::save(
      userId, original, passphrase);

  assert(crypto::IdentityKeyStore::exists(userId));

  // Load the identity.
  const crypto::KeyPair restored =
      crypto::IdentityKeyStore::load(userId, passphrase);

  assert(original.publicKey == restored.publicKey);
  assert(original.privateKey == restored.privateKey);

  // Incorrect passphrase must fail.
  bool wrongPassphraseRejected = false;

  try {
    crypto::IdentityKeyStore::load(
        userId, "incorrect-passphrase");
  } catch (const std::runtime_error&) {
    wrongPassphraseRejected = true;
  }

  assert(wrongPassphraseRejected);

  // Saving the same identity again must fail.
  bool overwriteRejected = false;

  try {
    crypto::IdentityKeyStore::save(
        userId, original, passphrase);
  } catch (const std::runtime_error&) {
    overwriteRejected = true;
  }

  assert(overwriteRejected);

  std::cout << "IdentityKeyStore tests passed\n";

  return 0;
}
