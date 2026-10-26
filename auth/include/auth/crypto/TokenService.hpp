#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "auth/crypto/JwtKeyManager.hpp"

namespace auth::crypto {

struct JwtClaims {
  std::string subject;
  std::string issuer;
  std::string tokenId;

  std::int64_t issuedAt;
  std::int64_t expiresAt;
};

class TokenService {
 public:
  // Generate a cryptographically secure random refresh token.
  static std::string generateRefreshToken();

  // Compute the SHA-256 hash stored in the session repository.
  static std::string hashRefreshToken(const std::string& token);

  // Generate an ES256-signed JWT using the supplied private key.
  static std::string generateAccessToken(const JwtClaims& claims,
                                         const JwtKeyManager& keyManager);

  // Verify the signature of an ES256-signed JWT using the supplied public key.
  static bool verifyAccessTokenSignature(const std::string& token,
                                         const JwtKeyManager& keyManager);

  // Validate the JWT signature, header and claims.
  static bool validateAccessToken(const std::string& token,
                                  const JwtKeyManager& keyManager);

 private:
  static std::string base64UrlEncode(const std::string& data);
  static std::string derToJose(const std::vector<unsigned char>& derSignature);
  static std::string base64UrlDecode(const std::string& encoded);
  static std::vector<unsigned char> joseToDer(const std::string& joseSignature);
  static nlohmann::json decodeJsonSegment(const std::string& encoded);
};

}  // namespace auth::crypto
