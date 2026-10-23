
#include "auth/crypto/JwtKeyManager.hpp"

#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/pem.h>

#include <cstdio>
#include <memory>
#include <stdexcept>

namespace auth::crypto {

namespace {

// Automatically close a file when leaving its scope.
struct FileDeleter {
  void operator()(FILE* file) const noexcept {
    if (file != nullptr) {
      std::fclose(file);
    }
  }
};

using FilePtr = std::unique_ptr<FILE, FileDeleter>;

void validateEs256Key(EVP_PKEY* key) {
  // ES256 requires an elliptic-curve key.
  if (EVP_PKEY_base_id(key) != EVP_PKEY_EC) {
    throw std::runtime_error("JWT key must be an EC key");
  }

  // ES256 specifically requires the P-256 curve.
  char curveName[80]{};
  size_t curveNameLength = 0;

  if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, curveName,
                                     sizeof(curveName),
                                     &curveNameLength) != 1) {
    throw std::runtime_error("Cannot determine JWT key curve");
  }

  if (std::string(curveName) != "prime256v1") {
    throw std::runtime_error("JWT key must use the P-256 curve");
  }
}

void validateKeyPair(EVP_PKEY* privateKey, EVP_PKEY* publicKey) {
  // Verify that the private key is structurally valid.
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context(
      EVP_PKEY_CTX_new(privateKey, nullptr), EVP_PKEY_CTX_free);

  if (!context || EVP_PKEY_private_check(context.get()) != 1) {
    throw std::runtime_error("Invalid JWT private key");
  }

  // Verify that the public key is structurally valid.
  std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> publicContext(
      EVP_PKEY_CTX_new(publicKey, nullptr), EVP_PKEY_CTX_free);

  if (!publicContext || EVP_PKEY_public_check(publicContext.get()) != 1) {
    throw std::runtime_error("Invalid JWT public key");
  }

  // Confirm that both keys belong to the same pair.
  if (EVP_PKEY_eq(privateKey, publicKey) != 1) {
    throw std::runtime_error("JWT public and private keys do not match");
  }
}

}  // namespace

JwtKeyManager::JwtKeyManager(const std::string& privateKeyPath,
                             const std::string& publicKeyPath) {
  // Open and load the private key.
  FilePtr privateFile(std::fopen(privateKeyPath.c_str(), "rb"));

  if (!privateFile) {
    throw std::runtime_error("Cannot open JWT private key file");
  }

  privateKey_.reset(
      PEM_read_PrivateKey(privateFile.get(), nullptr, nullptr, nullptr));

  if (!privateKey_) {
    throw std::runtime_error("Failed to load JWT private key");
  }

  // Open and load the public key.
  FilePtr publicFile(std::fopen(publicKeyPath.c_str(), "rb"));

  if (!publicFile) {
    throw std::runtime_error("Cannot open JWT public key file");
  }

  publicKey_.reset(
      PEM_read_PUBKEY(publicFile.get(), nullptr, nullptr, nullptr));

  if (!publicKey_) {
    throw std::runtime_error("Failed to load JWT public key");
  }
  // Validate that both keys use the P-256 curve.
  validateEs256Key(privateKey_.get());
  validateEs256Key(publicKey_.get());

  // Verify that both keys belong to the same pair.
  validateKeyPair(privateKey_.get(), publicKey_.get());
}

EVP_PKEY* JwtKeyManager::privateKey() const noexcept {
  return privateKey_.get();
}

EVP_PKEY* JwtKeyManager::publicKey() const noexcept { return publicKey_.get(); }

}  // namespace auth::crypto
