
#include "auth/services/AuthService.hpp"

#include <sodium.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>

#include "auth/crypto/PasswordHasher.hpp"
#include "auth/crypto/TokenService.hpp"

namespace auth::services {

namespace {

constexpr std::int64_t ACCESS_TOKEN_LIFETIME = 15 * 60;
constexpr std::int64_t REFRESH_TOKEN_LIFETIME = 7 * 24 * 60 * 60;

// Convert a Unix timestamp into an ISO 8601 UTC string.
std::string toIso8601(std::int64_t timestamp) {
  const std::time_t time = static_cast<std::time_t>(timestamp);

  std::tm utcTime{};

#ifdef _WIN32
  if (gmtime_s(&utcTime, &time) != 0) {
    throw std::runtime_error("Failed to convert timestamp");
  }
#else
  if (gmtime_r(&time, &utcTime) == nullptr) {
    throw std::runtime_error("Failed to convert timestamp");
  }
#endif

  std::array<char, 32> buffer{};

  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
                    &utcTime) == 0) {
    throw std::runtime_error("Failed to format timestamp");
  }

  return std::string(buffer.data());
}

}  // namespace

AuthService::AuthService(repositories::IUserRepository& userRepository,
                         repositories::ISessionRepository& sessionRepository,
                         const crypto::JwtKeyManager& jwtKeyManager)
    : userRepository_(userRepository),
      sessionRepository_(sessionRepository),
      jwtKeyManager_(jwtKeyManager) {}

std::optional<LoginResult> AuthService::login(const LoginRequest& request) {
  // Find the user by username.
  const auto user = userRepository_.findByUsername(request.username);

  if (!user.has_value()) {
    return std::nullopt;
  }

  // Verify the supplied password against the stored Argon2id hash.
  if (!crypto::PasswordHasher::verify(request.password, user->password_hash)) {
    return std::nullopt;
  }

  // Retrieve the current Unix timestamp.
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  // Generate the refresh token.
  const std::string refreshToken = crypto::TokenService::generateRefreshToken();

  // Store only its SHA-256 hash, never the raw refresh token.
  const std::string refreshTokenHash =
      crypto::TokenService::hashRefreshToken(refreshToken);

  // Prepare the new database session.
  domain::Session session{.id = "",
                          .user_id = user->id,
                          .refresh_token_hash = refreshTokenHash,
                          .issued_at = toIso8601(now),
                          .expires_at = toIso8601(now + REFRESH_TOKEN_LIFETIME),
                          .revoked_at = std::nullopt};

  // Persist the session and retrieve its generated identifier.
  const domain::Session createdSession = sessionRepository_.create(session);

  if (createdSession.id.empty()) {
    throw std::runtime_error("Session creation returned an empty ID");
  }

  // Prepare the JWT claims.
  const crypto::JwtClaims claims{.subject = user->id,
                                 .issuer = "securechat-auth",
                                 .tokenId = createdSession.id,
                                 .issuedAt = now,
                                 .expiresAt = now + ACCESS_TOKEN_LIFETIME};

  // Sign the access token using the Auth service private key.
  const std::string accessToken =
      crypto::TokenService::generateAccessToken(claims, jwtKeyManager_);

  // Return both tokens to the authenticated client.
  return LoginResult{.accessToken = accessToken, .refreshToken = refreshToken};
}

std::optional<RefreshResult> AuthService::refresh(
    const RefreshRequest& request) {
  // Reject an empty refresh token.
  if (request.refreshToken.empty()) {
    return std::nullopt;
  }

  // Hash the supplied refresh token.
  const std::string refreshTokenHash =
      crypto::TokenService::hashRefreshToken(request.refreshToken);

  // Retrieve the session associated with this token.
  const auto currentSession =
      sessionRepository_.findByRefreshTokenHash(refreshTokenHash);

  // Reject an unknown refresh token.
  if (!currentSession.has_value()) {
    return std::nullopt;
  }

  // Retrieve the current Unix timestamp.
  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

  // Generate a new refresh token.
  const std::string newRefreshToken =
      crypto::TokenService::generateRefreshToken();

  // Store only its SHA-256 hash.
  const std::string newRefreshTokenHash =
      crypto::TokenService::hashRefreshToken(newRefreshToken);

  // Prepare the replacement session.
  domain::Session newSession{
      .id = "",
      .user_id = currentSession->user_id,
      .refresh_token_hash = newRefreshTokenHash,
      .issued_at = toIso8601(now),
      .expires_at = toIso8601(now + REFRESH_TOKEN_LIFETIME),
      .revoked_at = std::nullopt};

  // Atomically revoke the old session and create its replacement.
  const auto rotatedSession =
      sessionRepository_.rotate(refreshTokenHash, newSession);

  // Reject the refresh request if rotation fails.
  if (!rotatedSession.has_value()) {
    return std::nullopt;
  }

  // Ensure the new session has a valid identifier.
  if (rotatedSession->id.empty()) {
    throw std::runtime_error("Session rotation returned an empty ID");
  }

  // Prepare the JWT claims.
  const crypto::JwtClaims claims{.subject = rotatedSession->user_id,
                                 .issuer = "securechat-auth",
                                 .tokenId = rotatedSession->id,
                                 .issuedAt = now,
                                 .expiresAt = now + ACCESS_TOKEN_LIFETIME};

  // Sign the new access token using the Auth service private key.
  const std::string accessToken =
      crypto::TokenService::generateAccessToken(claims, jwtKeyManager_);

  // Return the new access token and refresh token.
  return RefreshResult{.accessToken = accessToken,
                       .refreshToken = newRefreshToken};
}

}  // namespace auth::services
