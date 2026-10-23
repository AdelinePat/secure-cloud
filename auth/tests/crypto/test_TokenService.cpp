#include <gtest/gtest.h>
#include <sodium.h>

#include <chrono>
#include <string>

#include "auth/crypto/TokenService.hpp"

namespace {

class TokenServiceTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_GE(sodium_init(), 0); }
};

TEST_F(TokenServiceTest, GeneratesDifferentRefreshTokens) {
  const std::string token1 = auth::crypto::TokenService::generateRefreshToken();

  const std::string token2 = auth::crypto::TokenService::generateRefreshToken();

  EXPECT_FALSE(token1.empty());
  EXPECT_NE(token1, token2);
}

TEST_F(TokenServiceTest, GeneratesUrlSafeRefreshToken) {
  const std::string token = auth::crypto::TokenService::generateRefreshToken();

  // 32 random bytes produce 43 Base64 characters without padding.
  EXPECT_EQ(token.size(), 43u);

  EXPECT_EQ(token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                    "abcdefghijklmnopqrstuvwxyz"
                                    "0123456789-_"),
            std::string::npos);
}

TEST_F(TokenServiceTest, HashIsDeterministic) {
  const std::string token = auth::crypto::TokenService::generateRefreshToken();

  const std::string hash1 = auth::crypto::TokenService::hashRefreshToken(token);

  const std::string hash2 = auth::crypto::TokenService::hashRefreshToken(token);

  EXPECT_EQ(hash1, hash2);
  EXPECT_EQ(hash1.size(), 64u);
}

TEST_F(TokenServiceTest, GeneratesThreeBase64UrlEncodedSegments) {
  auth::crypto::JwtClaims claims{"user-123", "securechat-auth", "token-456",
                                 1700000000, 1700000900};

  auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const std::string result =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  const std::size_t separator = result.find('.');

  ASSERT_NE(separator, std::string::npos);

  // A signed JWT must contain exactly three segments.
  const std::size_t firstDot = result.find('.');

  ASSERT_NE(firstDot, std::string::npos);

  const std::size_t secondDot = result.find('.', firstDot + 1);

  ASSERT_NE(secondDot, std::string::npos);
  EXPECT_EQ(result.find('.', secondDot + 1), std::string::npos);

  // Extract the three JWT segments.
  const std::string encodedHeader = result.substr(0, firstDot);

  const std::string encodedPayload =
      result.substr(firstDot + 1, secondDot - firstDot - 1);

  const std::string encodedSignature = result.substr(secondDot + 1);

  // All three segments must be present.
  EXPECT_FALSE(encodedHeader.empty());
  EXPECT_FALSE(encodedPayload.empty());
  EXPECT_FALSE(encodedSignature.empty());

  // Base64URL must not contain standard Base64 characters or padding.
  EXPECT_EQ(result.find('+'), std::string::npos);
  EXPECT_EQ(result.find('/'), std::string::npos);
  EXPECT_EQ(result.find('='), std::string::npos);

  // A 64-byte ES256 signature produces 86 Base64URL characters without padding.
  EXPECT_EQ(encodedSignature.size(), 86);
}

TEST_F(TokenServiceTest, VerifiesValidAccessTokenSignature) {
  // Prepare the JWT claims.
  auth::crypto::JwtClaims claims{"user-123", "securechat-auth", "token-456",
                                 1700000000, 1700000900};

  // Load the test ES256 key pair.
  auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  // Generate a JWT signed with the private key.
  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // Verify its signature using the public key.
  EXPECT_TRUE(auth::crypto::TokenService::verifyAccessTokenSignature(
      token, keyManager));
}

TEST_F(TokenServiceTest, RejectsTamperedAccessToken) {
  auth::crypto::JwtClaims claims{"user-123", "securechat-auth", "token-456",
                                 1700000000, 1700000900};

  auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  // Generate a valid signed JWT.
  std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // Locate the payload segment.
  const std::size_t firstDot = token.find('.');
  const std::size_t secondDot = token.find('.', firstDot + 1);

  ASSERT_NE(firstDot, std::string::npos);
  ASSERT_NE(secondDot, std::string::npos);
  ASSERT_GT(secondDot, firstDot + 1);

  // Modify one Base64URL character without changing the signature.
  const std::size_t payloadPosition = firstDot + 1;

  token[payloadPosition] = (token[payloadPosition] == 'A') ? 'B' : 'A';

  // The original signature must no longer be valid.
  EXPECT_FALSE(auth::crypto::TokenService::verifyAccessTokenSignature(
      token, keyManager));
}

TEST_F(TokenServiceTest, AcceptsValidAccessToken) {
  // Load the test signing and verification keys.
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  // Retrieve the current Unix timestamp.
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  // Create claims for a token valid for 15 minutes.
  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now, now + 900};

  // Generate a signed JWT.
  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // Verify its signature, header and claims.
  EXPECT_TRUE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsExpiredAccessToken) {
  // Load the test signing and verification keys.
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  // Retrieve the current Unix timestamp.
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  // Create a token that expired one minute ago.
  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now - 960, now - 60};

  // Generate a correctly signed but expired JWT.
  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // A valid signature must not override token expiration.
  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithInvalidIssuer) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "unknown-issuer",
                                       "token-123", now, now + 900};

  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // The signature is valid, but the issuer is not trusted.
  EXPECT_TRUE(auth::crypto::TokenService::verifyAccessTokenSignature(
      token, keyManager));

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithEmptySubject) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"", "securechat-auth", "token-123", now,
                                       now + 900};

  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithEmptyTokenId) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth", "", now,
                                       now + 900};

  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenIssuedInFuture) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now + 3600, now + 4500};

  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithExpirationBeforeIssuance) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now, now - 60};

  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithEqualIssuanceAndExpiration) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now, now};

  const std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsMalformedAccessToken) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  EXPECT_FALSE(auth::crypto::TokenService::validateAccessToken("", keyManager));

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken("invalid", keyManager));

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken("a.b", keyManager));

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken("a.b.c.d", keyManager));

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken("..", keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithInvalidBase64Signature) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now, now + 900};

  std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // Replace the signature with invalid Base64URL characters.
  const std::size_t lastDot = token.rfind('.');

  ASSERT_NE(lastDot, std::string::npos);

  token.replace(lastDot + 1, std::string::npos, "!!!invalid!!!");

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

TEST_F(TokenServiceTest, RejectsAccessTokenWithTruncatedSignature) {
  const auth::crypto::JwtKeyManager keyManager(
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  const auth::crypto::JwtClaims claims{"user-123", "securechat-auth",
                                       "token-123", now, now + 900};

  std::string token =
      auth::crypto::TokenService::generateAccessToken(claims, keyManager);

  // Remove the last character of the signature.
  ASSERT_FALSE(token.empty());
  token.pop_back();

  EXPECT_FALSE(
      auth::crypto::TokenService::validateAccessToken(token, keyManager));
}

}  // namespace
