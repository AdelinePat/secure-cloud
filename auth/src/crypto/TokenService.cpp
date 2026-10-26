
#include "auth/crypto/TokenService.hpp"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <sodium.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace auth::crypto {

// ============================================================
// Refresh token generation
// ============================================================

std::string TokenService::generateRefreshToken() {
  // Generate 256 bits of cryptographically secure randomness.
  std::array<unsigned char, 32> tokenBytes{};

  randombytes_buf(tokenBytes.data(), tokenBytes.size());

  // Encode the token using URL-safe Base64 without padding.
  constexpr int variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;

  std::string token(sodium_base64_ENCODED_LEN(tokenBytes.size(), variant),
                    '\0');

  sodium_bin2base64(token.data(), token.size(), tokenBytes.data(),
                    tokenBytes.size(), variant);

  // Remove the terminating null character added by libsodium.
  token.resize(std::char_traits<char>::length(token.c_str()));

  return token;
}

// ============================================================
// Refresh token hashing
// ============================================================

std::string TokenService::hashRefreshToken(const std::string& token) {
  // Compute the SHA-256 digest of the refresh token.
  std::array<unsigned char, crypto_hash_sha256_BYTES> hash{};

  crypto_hash_sha256(hash.data(),
                     reinterpret_cast<const unsigned char*>(token.data()),
                     token.size());

  // Convert the binary digest to a hexadecimal string.
  std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hexHash{};

  sodium_bin2hex(hexHash.data(), hexHash.size(), hash.data(), hash.size());

  return std::string(hexHash.data());
}

// ============================================================
// Base64URL encoding
// ============================================================

std::string TokenService::base64UrlEncode(const std::string& data) {
  const std::size_t encodedSize = sodium_base64_encoded_len(
      data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);

  std::string encoded(encodedSize, '\0');

  sodium_bin2base64(encoded.data(), encoded.size(),
                    reinterpret_cast<const unsigned char*>(data.data()),
                    data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);

  // Remove the null terminator written by libsodium.
  encoded.resize(std::strlen(encoded.c_str()));

  return encoded;
}

// ============================================================
// Base64URL decoding
// ============================================================

std::string TokenService::base64UrlDecode(const std::string& encoded) {
  // Allocate enough space for the decoded binary data.
  std::string decoded(encoded.size(), '\0');

  std::size_t decodedSize = 0;

  // Decode using the URL-safe Base64 variant without padding.
  if (sodium_base642bin(reinterpret_cast<unsigned char*>(decoded.data()),
                        decoded.size(), encoded.data(), encoded.size(), nullptr,
                        &decodedSize, nullptr,
                        sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
    throw std::runtime_error("Invalid Base64URL encoding");
  }

  decoded.resize(decodedSize);

  return decoded;
}

// ============================================================
// JSON segment decoding
// ============================================================

nlohmann::json TokenService::decodeJsonSegment(const std::string& encoded) {
  // Decode the Base64URL segment into a JSON string.
  const std::string decoded = base64UrlDecode(encoded);

  // Parse the JSON string.
  return nlohmann::json::parse(decoded);
}

// ============================================================
// Convert ECDSA signature from DER to JOSE (R || S)
// ============================================================

std::string TokenService::derToJose(
    const std::vector<unsigned char>& derSignature) {
  // Decode the DER-encoded ECDSA signature.
  const unsigned char* data = derSignature.data();

  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(
      d2i_ECDSA_SIG(nullptr, &data, static_cast<long>(derSignature.size())),
      ECDSA_SIG_free);

  if (!signature || data != derSignature.data() + derSignature.size()) {
    throw std::runtime_error("Failed to decode DER signature");
  }

  // Extract the R and S components.
  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;

  ECDSA_SIG_get0(signature.get(), &r, &s);

  if (!r || !s) {
    throw std::runtime_error("Invalid ECDSA signature components");
  }

  // ES256 requires exactly 32 bytes for each component.
  std::string joseSignature(64, '\0');

  if (BN_bn2binpad(r, reinterpret_cast<unsigned char*>(joseSignature.data()),
                   32) != 32) {
    throw std::runtime_error("Failed to encode ECDSA R component");
  }

  if (BN_bn2binpad(s,
                   reinterpret_cast<unsigned char*>(joseSignature.data() + 32),
                   32) != 32) {
    throw std::runtime_error("Failed to encode ECDSA S component");
  }

  return joseSignature;
}

// ============================================================
// Convert ECDSA signature from JOSE (R || S) to DER
// ============================================================

std::vector<unsigned char> TokenService::joseToDer(
    const std::string& joseSignature) {
  // ES256 requires exactly 32 bytes for R and 32 bytes for S.
  if (joseSignature.size() != 64) {
    throw std::runtime_error("Invalid ES256 signature size");
  }

  // Convert the two binary components into OpenSSL BIGNUM objects.
  std::unique_ptr<BIGNUM, decltype(&BN_free)> r(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(joseSignature.data()),
                32, nullptr),
      BN_free);

  std::unique_ptr<BIGNUM, decltype(&BN_free)> s(
      BN_bin2bn(
          reinterpret_cast<const unsigned char*>(joseSignature.data() + 32), 32,
          nullptr),
      BN_free);

  if (!r || !s) {
    throw std::runtime_error("Failed to decode ECDSA components");
  }

  // Create an ECDSA signature structure.
  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(
      ECDSA_SIG_new(), ECDSA_SIG_free);

  if (!signature) {
    throw std::runtime_error("Failed to create ECDSA signature");
  }

  // Transfer ownership of R and S to the ECDSA_SIG structure.
  if (ECDSA_SIG_set0(signature.get(), r.get(), s.get()) != 1) {
    throw std::runtime_error("Failed to set ECDSA components");
  }

  r.release();
  s.release();

  // Determine the required DER buffer size.
  const int derSize = i2d_ECDSA_SIG(signature.get(), nullptr);

  if (derSize <= 0) {
    throw std::runtime_error("Failed to determine DER signature size");
  }

  std::vector<unsigned char> derSignature(static_cast<std::size_t>(derSize));

  unsigned char* output = derSignature.data();

  // Encode the ECDSA signature into DER.
  if (i2d_ECDSA_SIG(signature.get(), &output) != derSize) {
    throw std::runtime_error("Failed to encode DER signature");
  }

  return derSignature;
}

// ============================================================
// Generate an ES256 access token
// ============================================================

std::string TokenService::generateAccessToken(const JwtClaims& claims,
                                              const JwtKeyManager& keyManager) {
  // Build the JWT header.
  nlohmann::json header = {
      {"alg", "ES256"}, {"typ", "JWT"}, {"kid", "auth-key-1"}};

  // Build the JWT payload.
  nlohmann::json payload = {{"sub", claims.subject},
                            {"iss", claims.issuer},
                            {"jti", claims.tokenId},
                            {"iat", claims.issuedAt},
                            {"exp", claims.expiresAt}};

  const std::string serializedHeader = header.dump();
  const std::string serializedPayload = payload.dump();

  const std::string encodedHeader = base64UrlEncode(serializedHeader);
  const std::string encodedPayload = base64UrlEncode(serializedPayload);

  // The JWT signature covers the encoded header and payload.
  const std::string signingInput = encodedHeader + "." + encodedPayload;

  // Create and automatically release the OpenSSL signing context.
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);

  if (!context) {
    throw std::runtime_error("Failed to create JWT signing context");
  }

  // Initialize ECDSA signing with SHA-256.
  if (EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr,
                         keyManager.privateKey()) != 1) {
    throw std::runtime_error("Failed to initialize JWT signature");
  }

  // Sign the Base64URL-encoded header and payload.
  if (EVP_DigestSignUpdate(context.get(), signingInput.data(),
                           signingInput.size()) != 1) {
    throw std::runtime_error("Failed to update JWT signature");
  }

  // Determine the required signature buffer size.
  std::size_t signatureSize = 0;

  if (EVP_DigestSignFinal(context.get(), nullptr, &signatureSize) != 1) {
    throw std::runtime_error("Failed to determine JWT signature size");
  }

  std::vector<unsigned char> derSignature(signatureSize);

  // Generate the ECDSA signature in DER format.
  if (EVP_DigestSignFinal(context.get(), derSignature.data(), &signatureSize) !=
      1) {
    throw std::runtime_error("Failed to generate JWT signature");
  }

  derSignature.resize(signatureSize);

  // Convert the DER signature to the JOSE format.
  const std::string joseSignature = derToJose(derSignature);

  // Encode the signature and assemble the complete JWT.
  const std::string encodedSignature = base64UrlEncode(joseSignature);

  return signingInput + "." + encodedSignature;
}

// ============================================================
// Verify an ES256 access token signature
// ============================================================

bool TokenService::verifyAccessTokenSignature(const std::string& token,
                                              const JwtKeyManager& keyManager) {
  // Locate the JWT separators.
  const std::size_t firstDot = token.find('.');

  if (firstDot == std::string::npos) {
    return false;
  }

  const std::size_t secondDot = token.find('.', firstDot + 1);

  if (secondDot == std::string::npos ||
      token.find('.', secondDot + 1) != std::string::npos) {
    return false;
  }

  // Extract the three JWT segments.
  const std::string encodedHeader = token.substr(0, firstDot);

  const std::string encodedPayload =
      token.substr(firstDot + 1, secondDot - firstDot - 1);

  const std::string encodedSignature = token.substr(secondDot + 1);

  // Reject incomplete JWTs.
  if (encodedHeader.empty() || encodedPayload.empty() ||
      encodedSignature.empty()) {
    return false;
  }

  // Reconstruct the exact data originally signed.
  const std::string signingInput = token.substr(0, secondDot);

  // Decode the JWT signature.
  std::string joseSignature;

  try {
    joseSignature = base64UrlDecode(encodedSignature);
  } catch (const std::runtime_error&) {
    return false;
  }

  // An ES256 signature must contain exactly 64 bytes.
  if (joseSignature.size() != 64) {
    return false;
  }

  // Convert the JOSE signature into DER for OpenSSL verification.
  std::vector<unsigned char> derSignature;

  try {
    derSignature = joseToDer(joseSignature);
  } catch (const std::runtime_error&) {
    return false;
  }

  // Create the OpenSSL verification context.
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);

  if (!context) {
    throw std::runtime_error("Failed to create JWT verification context");
  }

  // Initialize ECDSA verification with SHA-256 and the public key.
  if (EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr,
                           keyManager.publicKey()) != 1) {
    throw std::runtime_error("Failed to initialize JWT verification");
  }

  // Provide the original JWT signing input.
  if (EVP_DigestVerifyUpdate(context.get(), signingInput.data(),
                             signingInput.size()) != 1) {
    throw std::runtime_error("Failed to update JWT verification");
  }

  // Verify the DER-encoded ECDSA signature.
  const int verificationResult = EVP_DigestVerifyFinal(
      context.get(), derSignature.data(), derSignature.size());

  if (verificationResult == 1) {
    return true;
  }

  if (verificationResult == 0) {
    return false;
  }

  throw std::runtime_error("JWT signature verification failed");
}

bool TokenService::validateAccessToken(const std::string& token,
                                       const JwtKeyManager& keyManager) {
  // Reject tokens with an invalid cryptographic signature.
  if (!verifyAccessTokenSignature(token, keyManager)) {
    return false;
  }

  // Locate the JWT segments.
  const std::size_t firstDot = token.find('.');
  const std::size_t secondDot = token.find('.', firstDot + 1);

  // Extract the encoded header and payload.
  const std::string encodedHeader = token.substr(0, firstDot);

  const std::string encodedPayload =
      token.substr(firstDot + 1, secondDot - firstDot - 1);

  try {
    // Decode and parse both JSON segments.
    const nlohmann::json header = decodeJsonSegment(encodedHeader);
    const nlohmann::json payload = decodeJsonSegment(encodedPayload);

    // Ensure the JWT header is a JSON object.
    if (!header.is_object()) {
      return false;
    }

    // Validate the expected JWT algorithm.
    if (!header.contains("alg") || !header["alg"].is_string() ||
        header["alg"] != "ES256") {
      return false;
    }

    // Validate the JWT type.
    if (!header.contains("typ") || !header["typ"].is_string() ||
        header["typ"] != "JWT") {
      return false;
    }

    // Validate the expected key identifier.
    if (!header.contains("kid") || !header["kid"].is_string() ||
        header["kid"] != "auth-key-1") {
      return false;
    }

    // Ensure the JWT payload is a JSON object.
    if (!payload.is_object()) {
      return false;
    }

    // Validate the subject.
    if (!payload.contains("sub") || !payload["sub"].is_string() ||
        payload["sub"].get<std::string>().empty()) {
      return false;
    }

    // Validate the token issuer.
    if (!payload.contains("iss") || !payload["iss"].is_string() ||
        payload["iss"] != "securechat-auth") {
      return false;
    }

    // Validate the token identifier.
    if (!payload.contains("jti") || !payload["jti"].is_string() ||
        payload["jti"].get<std::string>().empty()) {
      return false;
    }

    // Ensure timestamps are signed integers.
    if (!payload.contains("iat") || !payload["iat"].is_number_integer() ||
        !payload.contains("exp") || !payload["exp"].is_number_integer()) {
      return false;
    }

    const std::int64_t issuedAt = payload["iat"].get<std::int64_t>();
    const std::int64_t expiresAt = payload["exp"].get<std::int64_t>();

    // Retrieve the current Unix timestamp.
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    // Reject tokens issued in the future.
    if (issuedAt > now) {
      return false;
    }

    // Reject expired tokens.
    if (expiresAt <= now) {
      return false;
    }

    // Ensure expiration occurs after issuance.
    if (expiresAt <= issuedAt) {
      return false;
    }

    // The signature, header and claims are valid.
    return true;

  } catch (const nlohmann::json::exception&) {
    // Reject malformed JSON or invalid JSON field access.
    return false;

  } catch (const std::runtime_error&) {
    // Reject invalid Base64URL encoding.
    return false;
  }
}

}  // namespace auth::crypto
