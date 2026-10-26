#include <gtest/gtest.h>

#include "auth/crypto/JwtKeyManager.hpp"
#include "auth/crypto/PasswordHasher.hpp"
#include "auth/crypto/TokenService.hpp"
#include "auth/services/AuthService.hpp"
#include "repositories/InMemorySessionRepository.hpp"
#include "repositories/InMemoryUserRepository.hpp"

namespace {

TEST(AuthServiceTests, RefreshValidToken) {
  // Create in-memory repositories.
  auth::repositories::test::InMemoryUserRepository userRepository;
  auth::repositories::test::InMemorySessionRepository sessionRepository;

  // Load the ES256 test key pair.
  const std::string privateKeyPath =
      "/app/auth/tests/fixtures/test-private.pem";

  const std::string publicKeyPath = "/app/auth/tests/fixtures/test-public.pem";

  auth::crypto::JwtKeyManager jwtKeyManager(privateKeyPath, publicKeyPath);

  // Create the authentication service.
  auth::services::AuthService authService(userRepository, sessionRepository,
                                          jwtKeyManager);

  // Create a test user with an Argon2id password hash.
  auth::domain::User user{
      .id = "alice-id",
      .username = "alice",
      .email = "alice@example.com",
      .password_hash = auth::crypto::PasswordHasher::hash("SecurePassword123!"),
      .public_key = "",
      .key_algorithm = "X25519",
      .created_at = "",
      .updated_at = ""};

  // Insert the user into the in-memory repository.
  userRepository.seed(user);

  // Authenticate Alice to obtain the initial tokens.
  const auto loginResult = authService.login(
      {.username = "alice", .password = "SecurePassword123!"});

  // Verify that authentication succeeded.
  ASSERT_TRUE(loginResult.has_value());

  EXPECT_FALSE(loginResult->accessToken.empty());
  EXPECT_FALSE(loginResult->refreshToken.empty());

  // Refresh Alice's session using the initial refresh token.
const auto refreshResult = authService.refresh({
    .refreshToken = loginResult->refreshToken
});

// Verify that the refresh operation succeeded.
ASSERT_TRUE(refreshResult.has_value());

// Verify that both new tokens were generated.
EXPECT_FALSE(refreshResult->accessToken.empty());
EXPECT_FALSE(refreshResult->refreshToken.empty());

// Verify that the refresh token has been rotated.
EXPECT_NE(
    refreshResult->refreshToken,
    loginResult->refreshToken);
}

}  // namespace