
#include <gtest/gtest.h>

#include "auth/crypto/JwtKeyManager.hpp"

namespace {

TEST(JwtKeyManagerTest, LoadsValidEs256KeyPair) {
  const std::string privateKeyPath =
      "/app/auth/tests/fixtures/test-private.pem";

  const std::string publicKeyPath = "/app/auth/tests/fixtures/test-public.pem";

  ASSERT_NO_THROW({
    auth::crypto::JwtKeyManager keyManager(privateKeyPath, publicKeyPath);

    EXPECT_NE(keyManager.privateKey(), nullptr);
    EXPECT_NE(keyManager.publicKey(), nullptr);
  });
}

TEST(JwtKeyManagerTest, RejectsMissingPrivateKey) {
  const std::string privateKeyPath =
      "/app/auth/tests/fixtures/missing-private.pem";

  const std::string publicKeyPath = "/app/auth/tests/fixtures/test-public.pem";

  EXPECT_THROW(
      auth::crypto::JwtKeyManager keyManager(privateKeyPath, publicKeyPath),
      std::runtime_error);
}

TEST(JwtKeyManagerTest, RejectsMismatchedKeyPair) {
  // Use the private key from pair A and the public key from pair B.
  const std::string privateKeyPath =
      "/app/auth/tests/fixtures/test-private.pem";

  const std::string publicKeyPath = "/app/auth/tests/fixtures/other-public.pem";

  EXPECT_THROW(
      auth::crypto::JwtKeyManager keyManager(privateKeyPath, publicKeyPath),
      std::runtime_error);
}

}  // namespace
