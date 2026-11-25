#pragma once

#include <gtest/gtest.h>
#include <openssl/ecdsa.h>
#include <openssl/pem.h>
#include <sodium.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace test_jwt {

inline constexpr char subject[] = "01234567-89ab-4def-8123-456789abcdef";

inline std::int64_t now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

inline nlohmann::json claims() {
  const auto timestamp = now();
  return {{"sub", subject}, {"iss", "securechat-auth"}, {"aud", "securechat-api"},
          {"jti", "test-token"}, {"iat", timestamp - 1}, {"exp", timestamp + 899}};
}

inline std::string encode(const unsigned char* bytes, std::size_t size) {
  constexpr int variant = sodium_base64_VARIANT_URLSAFE_NO_PADDING;
  std::string result(sodium_base64_encoded_len(size, variant), '\0');
  sodium_bin2base64(result.data(), result.size(), bytes, size, variant);
  result.resize(result.find('\0'));
  return result;
}

inline std::string encode(const nlohmann::json& value) {
  const auto data = value.dump();
  return encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

inline void sign(const nlohmann::json& payload, std::string& token,
                 const char* algorithm = "ES256") {
  std::unique_ptr<FILE, decltype(&std::fclose)> pem(
      std::fopen(SECURECHAT_TEST_KEYS "/test-private.pem", "rb"), std::fclose);
  ASSERT_TRUE(pem);
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      PEM_read_PrivateKey(pem.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
  ASSERT_TRUE(key);
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(
      EVP_MD_CTX_new(), EVP_MD_CTX_free);
  ASSERT_TRUE(context);
  ASSERT_EQ(EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr,
                               key.get()), 1);
  const auto input = encode(nlohmann::json{{"alg", algorithm}, {"typ", "JWT"},
                                           {"kid", "auth-key-1"}}) +
                     "." + encode(payload);
  const auto* bytes = reinterpret_cast<const unsigned char*>(input.data());
  std::size_t size = 0;
  ASSERT_EQ(EVP_DigestSign(context.get(), nullptr, &size, bytes, input.size()), 1);
  std::vector<unsigned char> der(size);
  ASSERT_EQ(EVP_DigestSign(context.get(), der.data(), &size, bytes, input.size()), 1);
  const unsigned char* ptr = der.data();
  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> signature(
      d2i_ECDSA_SIG(nullptr, &ptr, static_cast<long>(size)), ECDSA_SIG_free);
  ASSERT_TRUE(signature);
  std::array<unsigned char, 64> jose{};
  ASSERT_EQ(BN_bn2binpad(ECDSA_SIG_get0_r(signature.get()), jose.data(), 32), 32);
  ASSERT_EQ(BN_bn2binpad(ECDSA_SIG_get0_s(signature.get()), jose.data() + 32, 32), 32);
  token = input + "." + encode(jose.data(), jose.size());
}

}  // namespace test_jwt
