#include "messaging/crypto/JwtVerifier.hpp"

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <sodium.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace messaging::crypto {

namespace {

using json = nlohmann::json;

struct FileDeleter {
  void operator()(FILE* file) const noexcept {
    if (file != nullptr) std::fclose(file);
  }
};

using FilePtr = std::unique_ptr<FILE, FileDeleter>;

std::string base64UrlDecode(const std::string& encoded) {
  std::string decoded(encoded.size(), '\0');
  std::size_t decodedSize = 0;

  if (sodium_base642bin(
          reinterpret_cast<unsigned char*>(decoded.data()), decoded.size(),
          encoded.data(), encoded.size(), nullptr, &decodedSize, nullptr,
          sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
    throw std::runtime_error("Invalid Base64URL encoding");
  }

  decoded.resize(decodedSize);
  return decoded;
}

// JWT ES256 signatures are JOSE (R || S); OpenSSL expects DER.
std::vector<unsigned char> joseToDer(const std::string& joseSignature) {
  if (joseSignature.size() != 64) {
    throw std::runtime_error("Invalid ES256 signature size");
  }

  std::unique_ptr<BIGNUM, decltype(&BN_free)> r(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(joseSignature.data()),
                32, nullptr),
      BN_free);
  std::unique_ptr<BIGNUM, decltype(&BN_free)> s(
      BN_bin2bn(
          reinterpret_cast<const unsigned char*>(joseSignature.data() + 32), 32,
          nullptr),
      BN_free);

  if (!r || !s) throw std::runtime_error("Failed to decode ECDSA components");

  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(
      ECDSA_SIG_new(), ECDSA_SIG_free);
  if (!signature) throw std::runtime_error("Failed to create ECDSA signature");

  if (ECDSA_SIG_set0(signature.get(), r.get(), s.get()) != 1) {
    throw std::runtime_error("Failed to set ECDSA components");
  }
  r.release();
  s.release();

  const int derSize = i2d_ECDSA_SIG(signature.get(), nullptr);
  if (derSize <= 0) {
    throw std::runtime_error("Failed to determine DER signature size");
  }

  std::vector<unsigned char> derSignature(static_cast<std::size_t>(derSize));
  unsigned char* output = derSignature.data();
  if (i2d_ECDSA_SIG(signature.get(), &output) != derSize) {
    throw std::runtime_error("Failed to encode DER signature");
  }

  return derSignature;
}

bool verifySignature(EVP_PKEY* publicKey, const std::string& signingInput,
                     const std::string& joseSignature) {
  const std::vector<unsigned char> derSignature = joseToDer(joseSignature);

  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!context) throw std::runtime_error("Failed to create verification context");

  if (EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr,
                           publicKey) != 1) {
    throw std::runtime_error("Failed to initialize JWT verification");
  }

  if (EVP_DigestVerifyUpdate(context.get(), signingInput.data(),
                             signingInput.size()) != 1) {
    throw std::runtime_error("Failed to update JWT verification");
  }

  return EVP_DigestVerifyFinal(context.get(), derSignature.data(),
                               derSignature.size()) == 1;
}

}  // namespace

JwtVerifier::JwtVerifier(const std::string& publicKeyPath) {
  FilePtr file(std::fopen(publicKeyPath.c_str(), "rb"));
  if (!file) {
    throw std::runtime_error("Cannot open JWT public key file: " +
                             publicKeyPath);
  }

  publicKey_.reset(PEM_read_PUBKEY(file.get(), nullptr, nullptr, nullptr));
  if (!publicKey_) {
    throw std::runtime_error("Failed to load JWT public key");
  }
}

std::optional<std::string> JwtVerifier::verify(
    const std::string& token) const {
  const std::size_t firstDot = token.find('.');
  if (firstDot == std::string::npos) return std::nullopt;

  const std::size_t secondDot = token.find('.', firstDot + 1);
  if (secondDot == std::string::npos ||
      token.find('.', secondDot + 1) != std::string::npos) {
    return std::nullopt;
  }

  const std::string encodedHeader = token.substr(0, firstDot);
  const std::string encodedPayload =
      token.substr(firstDot + 1, secondDot - firstDot - 1);
  const std::string encodedSignature = token.substr(secondDot + 1);

  if (encodedHeader.empty() || encodedPayload.empty() ||
      encodedSignature.empty()) {
    return std::nullopt;
  }

  try {
    const json header = json::parse(base64UrlDecode(encodedHeader));
    const json payload = json::parse(base64UrlDecode(encodedPayload));

    if (!header.is_object() || !payload.is_object()) return std::nullopt;

    if (!header.contains("alg") || header["alg"] != "ES256") {
      return std::nullopt;
    }
    if (!header.contains("typ") || header["typ"] != "JWT") {
      return std::nullopt;
    }
    if (!header.contains("kid") || header["kid"] != "auth-key-1") {
      return std::nullopt;
    }

    if (!payload.contains("sub") || !payload["sub"].is_string() ||
        payload["sub"].get<std::string>().empty()) {
      return std::nullopt;
    }
    if (!payload.contains("iss") || payload["iss"] != "securechat-auth") {
      return std::nullopt;
    }
    if (!payload.contains("iat") || !payload["iat"].is_number_integer() ||
        !payload.contains("exp") || !payload["exp"].is_number_integer()) {
      return std::nullopt;
    }

    const std::int64_t issuedAt = payload["iat"].get<std::int64_t>();
    const std::int64_t expiresAt = payload["exp"].get<std::int64_t>();

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    if (issuedAt > now || expiresAt <= now || expiresAt <= issuedAt) {
      return std::nullopt;
    }

    const std::string signingInput = token.substr(0, secondDot);
    const std::string joseSignature = base64UrlDecode(encodedSignature);

    if (!verifySignature(publicKey_.get(), signingInput, joseSignature)) {
      return std::nullopt;
    }

    return payload["sub"].get<std::string>();

  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace messaging::crypto
