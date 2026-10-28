#include "messaging/security/JwtVerifier.hpp"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ecdsa.h>
#include <openssl/params.h>
#include <openssl/pem.h>
#include <sodium.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

namespace messaging::security {

namespace {

std::string decodeBase64Url(const std::string& encoded) {
  std::string decoded(encoded.size(), '\0');
  std::size_t decodedSize = 0;

  if (sodium_base642bin(reinterpret_cast<unsigned char*>(decoded.data()),
                        decoded.size(), encoded.data(), encoded.size(), nullptr,
                        &decodedSize, nullptr,
                        sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0) {
    throw std::runtime_error("Invalid Base64URL encoding");
  }

  decoded.resize(decodedSize);
  return decoded;
}

std::vector<unsigned char> joseToDer(const std::string& signature) {
  if (signature.size() != 64) {
    throw std::runtime_error("Invalid ES256 signature size");
  }

  std::unique_ptr<BIGNUM, decltype(&BN_free)> r(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(signature.data()), 32,
                nullptr),
      BN_free);

  std::unique_ptr<BIGNUM, decltype(&BN_free)> s(
      BN_bin2bn(reinterpret_cast<const unsigned char*>(signature.data() + 32),
                32, nullptr),
      BN_free);

  if (!r || !s) {
    throw std::runtime_error("Invalid signature components");
  }

  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> ecdsaSignature(
      ECDSA_SIG_new(), ECDSA_SIG_free);

  if (!ecdsaSignature ||
      ECDSA_SIG_set0(ecdsaSignature.get(), r.get(), s.get()) != 1) {
    throw std::runtime_error("Cannot construct ECDSA signature");
  }

  r.release();
  s.release();

  const int derSize = i2d_ECDSA_SIG(ecdsaSignature.get(), nullptr);

  if (derSize <= 0) {
    throw std::runtime_error("Cannot encode ECDSA signature");
  }

  std::vector<unsigned char> der(static_cast<std::size_t>(derSize));
  unsigned char* output = der.data();

  if (i2d_ECDSA_SIG(ecdsaSignature.get(), &output) != derSize) {
    throw std::runtime_error("Cannot encode ECDSA signature");
  }

  return der;
}

bool verifySignature(const std::string& signingInput,
                     const std::vector<unsigned char>& signature,
                     EVP_PKEY* publicKey) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);

  if (!context ||
      EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr,
                           publicKey) != 1 ||
      EVP_DigestVerifyUpdate(context.get(), signingInput.data(),
                             signingInput.size()) != 1) {
    return false;
  }

  return EVP_DigestVerifyFinal(context.get(), signature.data(),
                               signature.size()) == 1;
}

}  // namespace

JwtVerifier::JwtVerifier(const std::string& publicKeyPath) {
  // Automatically close the public key file.
struct FileDeleter {
  void operator()(FILE* file) const noexcept {
    if (file != nullptr) {
      std::fclose(file);
    }
  }
};

std::unique_ptr<FILE, FileDeleter> file(
    std::fopen(publicKeyPath.c_str(), "rb"));

  if (!file) {
    throw std::runtime_error("Cannot open JWT public key");
  }

  publicKey_.reset(PEM_read_PUBKEY(file.get(), nullptr, nullptr, nullptr));

  if (!publicKey_ || EVP_PKEY_base_id(publicKey_.get()) != EVP_PKEY_EC) {
    throw std::runtime_error("Invalid JWT public key");
  }

  char curveName[80]{};
  size_t curveNameLength = 0;

  if (EVP_PKEY_get_utf8_string_param(
          publicKey_.get(), OSSL_PKEY_PARAM_GROUP_NAME, curveName,
          sizeof(curveName), &curveNameLength) != 1 ||
      std::string(curveName) != "prime256v1") {
    throw std::runtime_error("JWT public key must use P-256");
  }
}

std::optional<std::string> JwtVerifier::verify(const std::string& token) const {
  try {
    const auto firstDot = token.find('.');
    if (firstDot == std::string::npos) {
      return std::nullopt;
    }

    const auto secondDot = token.find('.', firstDot + 1);
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

    const auto header = nlohmann::json::parse(decodeBase64Url(encodedHeader));
    const auto payload = nlohmann::json::parse(decodeBase64Url(encodedPayload));

    if (!header.is_object() || header.value("alg", "") != "ES256" ||
        header.value("typ", "") != "JWT" ||
        header.value("kid", "") != "auth-key-1") {
      return std::nullopt;
    }

    if (!payload.is_object() || !payload.contains("sub") ||
        !payload["sub"].is_string() || !payload.contains("iss") ||
        !payload["iss"].is_string() || payload["iss"] != "securechat-auth" ||
        !payload.contains("jti") || !payload["jti"].is_string() ||
        !payload.contains("iat") || !payload["iat"].is_number_integer() ||
        !payload.contains("exp") || !payload["exp"].is_number_integer()) {
      return std::nullopt;
    }

    const auto subject = payload["sub"].get<std::string>();
    const auto tokenId = payload["jti"].get<std::string>();
    const auto issuedAt = payload["iat"].get<std::int64_t>();
    const auto expiresAt = payload["exp"].get<std::int64_t>();

    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();

    if (subject.empty() || tokenId.empty() || issuedAt > now ||
        expiresAt <= now || expiresAt <= issuedAt) {
      return std::nullopt;
    }

    const std::string signingInput = token.substr(0, secondDot);
    const auto signature = joseToDer(decodeBase64Url(encodedSignature));

    if (!verifySignature(signingInput, signature, publicKey_.get())) {
      return std::nullopt;
    }

    return subject;

  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace messaging::security