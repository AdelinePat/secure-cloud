#include "common/jwt/JwtVerifier.hpp"

#include <gtest/gtest.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <sodium.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include <unistd.h>

using common::jwt::JwtVerifier;

namespace {

using PKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

PKey generateP256() {
  return PKey(EVP_EC_gen("P-256"), EVP_PKEY_free);
}

std::int64_t now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string base64Url(const std::string& data) {
  std::string out(sodium_base64_ENCODED_LEN(
                      data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING),
                  '\0');
  sodium_bin2base64(out.data(), out.size(),
                    reinterpret_cast<const unsigned char*>(data.data()),
                    data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  out.resize(std::char_traits<char>::length(out.c_str()));
  return out;
}

// ES256 signature in JOSE form (r || s, 32 bytes each).
std::string signEs256(const std::string& input, EVP_PKEY* key) {
  std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),
                                                             EVP_MD_CTX_free);
  std::size_t derLen = 0;
  if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key) != 1 ||
      EVP_DigestSign(ctx.get(), nullptr, &derLen,
                     reinterpret_cast<const unsigned char*>(input.data()),
                     input.size()) != 1) {
    throw std::runtime_error("sign init failed");
  }
  std::string der(derLen, '\0');
  if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(der.data()),
                     &derLen,
                     reinterpret_cast<const unsigned char*>(input.data()),
                     input.size()) != 1) {
    throw std::runtime_error("sign failed");
  }

  const auto* p = reinterpret_cast<const unsigned char*>(der.data());
  std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)> sig(
      d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(derLen)), ECDSA_SIG_free);
  std::string jose(64, '\0');
  BN_bn2binpad(ECDSA_SIG_get0_r(sig.get()),
               reinterpret_cast<unsigned char*>(jose.data()), 32);
  BN_bn2binpad(ECDSA_SIG_get0_s(sig.get()),
               reinterpret_cast<unsigned char*>(jose.data() + 32), 32);
  return jose;
}

nlohmann::json validHeader() {
  return {{"alg", "ES256"}, {"typ", "JWT"}, {"kid", "auth-key-1"}};
}

nlohmann::json validPayload() {
  const auto t = now();
  return {{"sub", "11111111-1111-1111-1111-111111111111"},
          {"iss", "securechat-auth"},
          {"jti", "token-id"},
          {"iat", t - 10},
          {"exp", t + 600}};
}

std::string makeToken(const nlohmann::json& header,
                      const nlohmann::json& payload, EVP_PKEY* key) {
  const std::string input =
      base64Url(header.dump()) + "." + base64Url(payload.dump());
  return input + "." + base64Url(signEs256(input, key));
}

class JwtVerifierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_GE(sodium_init(), 0);
    ASSERT_TRUE(key_);
    ASSERT_TRUE(otherKey_);
    publicKeyPath_ = std::filesystem::temp_directory_path() /
                     ("jwt-test-" + std::to_string(::getpid()) + ".pem");
    writePublicKey(key_.get(), publicKeyPath_);
  }

  void TearDown() override { std::filesystem::remove(publicKeyPath_); }

  static void writePublicKey(EVP_PKEY* key, const std::filesystem::path& path) {
    struct FileCloser {
      void operator()(FILE* f) const noexcept { std::fclose(f); }
    };
    std::unique_ptr<FILE, FileCloser> f(std::fopen(path.c_str(), "wb"));
    ASSERT_TRUE(f);
    ASSERT_EQ(PEM_write_PUBKEY(f.get(), key), 1);
  }

  PKey key_ = generateP256();
  PKey otherKey_ = generateP256();
  std::filesystem::path publicKeyPath_;
};

}  // namespace

TEST_F(JwtVerifierTest, AcceptsValidTokenAndReturnsSubject) {
  JwtVerifier verifier(publicKeyPath_.string());
  const auto subject =
      verifier.verify(makeToken(validHeader(), validPayload(), key_.get()));
  ASSERT_TRUE(subject.has_value());
  EXPECT_EQ(*subject, "11111111-1111-1111-1111-111111111111");
}

TEST_F(JwtVerifierTest, RejectsTokenSignedByAnotherKey) {
  JwtVerifier verifier(publicKeyPath_.string());
  EXPECT_FALSE(
      verifier.verify(makeToken(validHeader(), validPayload(), otherKey_.get())));
}

TEST_F(JwtVerifierTest, RejectsExpiredToken) {
  JwtVerifier verifier(publicKeyPath_.string());
  auto payload = validPayload();
  payload["iat"] = now() - 1200;
  payload["exp"] = now() - 600;
  EXPECT_FALSE(verifier.verify(makeToken(validHeader(), payload, key_.get())));
}

TEST_F(JwtVerifierTest, RejectsWrongIssuer) {
  JwtVerifier verifier(publicKeyPath_.string());
  auto payload = validPayload();
  payload["iss"] = "someone-else";
  EXPECT_FALSE(verifier.verify(makeToken(validHeader(), payload, key_.get())));
}

TEST_F(JwtVerifierTest, RejectsOtherAlgorithm) {
  JwtVerifier verifier(publicKeyPath_.string());
  auto header = validHeader();
  header["alg"] = "none";
  EXPECT_FALSE(verifier.verify(makeToken(header, validPayload(), key_.get())));
}

TEST_F(JwtVerifierTest, RejectsTamperedPayload) {
  JwtVerifier verifier(publicKeyPath_.string());
  const auto token = makeToken(validHeader(), validPayload(), key_.get());
  auto forged = validPayload();
  forged["sub"] = "22222222-2222-2222-2222-222222222222";

  const auto firstDot = token.find('.');
  const auto secondDot = token.find('.', firstDot + 1);
  const std::string tampered = token.substr(0, firstDot + 1) +
                               base64Url(forged.dump()) +
                               token.substr(secondDot);
  EXPECT_FALSE(verifier.verify(tampered));
}

TEST_F(JwtVerifierTest, RejectsMalformedTokens) {
  JwtVerifier verifier(publicKeyPath_.string());
  EXPECT_FALSE(verifier.verify(""));
  EXPECT_FALSE(verifier.verify("abc"));
  EXPECT_FALSE(verifier.verify("a.b"));
  EXPECT_FALSE(verifier.verify("a.b.c.d"));
  EXPECT_FALSE(verifier.verify("!!.??.**"));
}

TEST_F(JwtVerifierTest, ConstructorRejectsMissingKeyFile) {
  EXPECT_THROW(JwtVerifier("/nonexistent/key.pem"), std::runtime_error);
}

TEST_F(JwtVerifierTest, ConstructorRejectsNonP256Key) {
  PKey ed(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"), EVP_PKEY_free);
  ASSERT_TRUE(ed);
  writePublicKey(ed.get(), publicKeyPath_);
  EXPECT_THROW(JwtVerifier(publicKeyPath_.string()), std::runtime_error);
}
