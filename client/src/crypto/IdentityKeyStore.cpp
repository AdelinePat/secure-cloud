
#include "crypto/IdentityKeyStore.hpp"

#include <sodium.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace crypto {

namespace {

QString identityFilePath(const QString& userId) {
  const QUuid uuid(userId);

  if (uuid.isNull()) {
    throw std::invalid_argument("Invalid user ID");
  }

  const QString appDataPath =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  if (appDataPath.isEmpty()) {
    throw std::runtime_error("Application data directory unavailable");
  }

  const QString fileName = uuid.toString(QUuid::WithoutBraces) + ".key";

  return QDir(appDataPath).filePath("keys/" + fileName);
}
constexpr unsigned char KEY_FILE_VERSION = 1;

constexpr std::size_t IDENTITY_KEY_SIZE =
    crypto_kx_PUBLICKEYBYTES + crypto_kx_SECRETKEYBYTES;

constexpr std::size_t ENCRYPTED_IDENTITY_SIZE =
    IDENTITY_KEY_SIZE + crypto_secretbox_MACBYTES;

}  // namespace

bool IdentityKeyStore::exists(const QString& userId) {
  if (userId.isEmpty()) {
    return false;
  }

  return QFileInfo::exists(identityFilePath(userId));
}

void IdentityKeyStore::save(const QString& userId, const KeyPair& keyPair,
                            const QString& passphrase) {
  if (passphrase.isEmpty()) {
    throw std::invalid_argument("Passphrase cannot be empty");
  }

  const QString filePath = identityFilePath(userId);

  // Never silently replace an existing identity.
  if (QFile::exists(filePath)) {
    throw std::runtime_error("Identity already exists");
  }

  const QFileInfo fileInfo(filePath);
  if (!QDir().mkpath(fileInfo.absolutePath())) {
    throw std::runtime_error("Cannot create identity directory");
  }

  QByteArray password = passphrase.toUtf8();

  std::array<unsigned char, crypto_pwhash_SALTBYTES> salt{};
  randombytes_buf(salt.data(), salt.size());

  std::array<unsigned char, crypto_secretbox_KEYBYTES> encryptionKey{};

  const unsigned long long opslimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
  const size_t memlimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;

  if (crypto_pwhash(
          encryptionKey.data(), encryptionKey.size(), password.constData(),
          static_cast<unsigned long long>(password.size()), salt.data(),
          opslimit, memlimit, crypto_pwhash_ALG_ARGON2ID13) != 0) {
    sodium_memzero(password.data(), password.size());
    throw std::runtime_error("Key derivation failed");
  }

  sodium_memzero(password.data(), password.size());

  std::array<unsigned char, IDENTITY_KEY_SIZE> plaintext{};
  std::copy(keyPair.publicKey.begin(), keyPair.publicKey.end(),
            plaintext.begin());
  std::copy(keyPair.privateKey.begin(), keyPair.privateKey.end(),
            plaintext.begin() + crypto_kx_PUBLICKEYBYTES);

  std::array<unsigned char, crypto_secretbox_NONCEBYTES> nonce{};
  randombytes_buf(nonce.data(), nonce.size());

  std::array<unsigned char, ENCRYPTED_IDENTITY_SIZE> ciphertext{};

  if (crypto_secretbox_easy(ciphertext.data(), plaintext.data(),
                            plaintext.size(), nonce.data(),
                            encryptionKey.data()) != 0) {
    sodium_memzero(plaintext.data(), plaintext.size());
    sodium_memzero(encryptionKey.data(), encryptionKey.size());
    throw std::runtime_error("Identity encryption failed");
  }

  sodium_memzero(plaintext.data(), plaintext.size());
  sodium_memzero(encryptionKey.data(), encryptionKey.size());

  QByteArray fileData;
  fileData.append(static_cast<char>(KEY_FILE_VERSION));
  fileData.append(reinterpret_cast<const char*>(salt.data()),
                  static_cast<qsizetype>(salt.size()));
  fileData.append(reinterpret_cast<const char*>(nonce.data()),
                  static_cast<qsizetype>(nonce.size()));
  fileData.append(reinterpret_cast<const char*>(ciphertext.data()),
                  static_cast<qsizetype>(ciphertext.size()));

  QSaveFile file(filePath);

  if (!file.open(QIODevice::WriteOnly)) {
    throw std::runtime_error("Cannot open identity file");
  }

  if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
    file.cancelWriting();
    throw std::runtime_error("Cannot restrict identity file permissions");
  }

  if (file.write(fileData) != fileData.size()) {
    file.cancelWriting();
    throw std::runtime_error("Cannot write identity file");
  }

  // Recheck before committing. This is not an atomic no-overwrite guarantee.
  if (QFile::exists(filePath)) {
    file.cancelWriting();
    throw std::runtime_error("Identity already exists");
  }

  if (!file.commit()) {
    throw std::runtime_error("Cannot save identity file");
  }
}

KeyPair IdentityKeyStore::load(const QString& userId,
                               const QString& passphrase) {
  if (passphrase.isEmpty()) {
    throw std::invalid_argument("Passphrase cannot be empty");
  }

  QFile file(identityFilePath(userId));

  if (!file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error("Cannot open identity file");
  }

  const QByteArray fileData = file.readAll();
  file.close();

  constexpr qsizetype expectedSize = 1 + crypto_pwhash_SALTBYTES +
                                     crypto_secretbox_NONCEBYTES +
                                     ENCRYPTED_IDENTITY_SIZE;

  if (fileData.size() != expectedSize) {
    throw std::runtime_error("Invalid identity file size");
  }

  const auto* data =
      reinterpret_cast<const unsigned char*>(fileData.constData());

  if (data[0] != KEY_FILE_VERSION) {
    throw std::runtime_error("Unsupported identity file version");
  }

  const unsigned char* salt = data + 1;
  const unsigned char* nonce = salt + crypto_pwhash_SALTBYTES;
  const unsigned char* ciphertext = nonce + crypto_secretbox_NONCEBYTES;

  QByteArray password = passphrase.toUtf8();

  std::array<unsigned char, crypto_secretbox_KEYBYTES> encryptionKey{};

  const int result = crypto_pwhash(
      encryptionKey.data(), encryptionKey.size(), password.constData(),
      static_cast<unsigned long long>(password.size()), salt,
      crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
      crypto_pwhash_ALG_ARGON2ID13);

  sodium_memzero(password.data(), password.size());

  if (result != 0) {
    throw std::runtime_error("Key derivation failed");
  }

  std::array<unsigned char, IDENTITY_KEY_SIZE> plaintext{};

  const int decryptResult = crypto_secretbox_open_easy(
      plaintext.data(), ciphertext, ENCRYPTED_IDENTITY_SIZE, nonce,
      encryptionKey.data());

  sodium_memzero(encryptionKey.data(), encryptionKey.size());

  if (decryptResult != 0) {
    throw std::runtime_error("Invalid passphrase or corrupted identity file");
  }

  KeyPair keyPair{};

  std::copy_n(plaintext.begin(), crypto_kx_PUBLICKEYBYTES,
              keyPair.publicKey.begin());

  std::copy_n(plaintext.begin() + crypto_kx_PUBLICKEYBYTES,
              crypto_kx_SECRETKEYBYTES, keyPair.privateKey.begin());

  sodium_memzero(plaintext.data(), plaintext.size());

  return keyPair;
}

}  // namespace crypto
