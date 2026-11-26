#include "SignedJwt.hpp"
#include "common/jwt/JwtVerifier.hpp"

namespace {

class JwtVerifierTest : public testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_GE(sodium_init(), 0); }

  common::jwt::JwtVerifier verifier{SECURECHAT_TEST_KEYS "/test-public.pem"};
  nlohmann::json claims = test_jwt::claims();
  std::string token;
};

TEST_F(JwtVerifierTest, AcceptsValidToken) {
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  const auto subject = verifier.verify(token);
  ASSERT_TRUE(subject);
  EXPECT_EQ(*subject, test_jwt::subject);
}

TEST_F(JwtVerifierTest, NormalizesUppercaseUuid) {
  claims["sub"] = "01234567-89AB-4DEF-8123-456789ABCDEF";
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  const auto subject = verifier.verify(token);
  ASSERT_TRUE(subject);
  EXPECT_EQ(*subject, test_jwt::subject);
}

TEST_F(JwtVerifierTest, RejectsChangedSignature) {
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  auto& first = token[token.rfind('.') + 1];
  first = first == 'A' ? 'B' : 'A';
  EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsWrongKey) {
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  common::jwt::JwtVerifier other{SECURECHAT_TEST_KEYS "/other-public.pem"};
  EXPECT_FALSE(other.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsExpiredToken) {
  claims["exp"] = test_jwt::now() - 1;
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsFutureToken) {
  claims["iat"] = test_jwt::now() + 120;
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsInvalidClaims) {
  const nlohmann::json invalidValues{{"iss", "other"}, {"aud", "other"},
                                     {"sub", "user-123"}};
  for (const auto& [claim, value] : invalidValues.items()) {
    SCOPED_TRACE(claim);
    auto invalid = claims;
    invalid[claim] = value;
    ASSERT_NO_FATAL_FAILURE(test_jwt::sign(invalid, token));
    EXPECT_FALSE(verifier.verify(token).has_value());
  }
  claims.erase("aud");
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsWrongAlgorithm) {
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token, "ES384"));
  EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsMalformedSignature) {
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  token.replace(token.rfind('.') + 1, std::string::npos, "AA");
  EXPECT_FALSE(verifier.verify(token).has_value());
}

TEST_F(JwtVerifierTest, RejectsInvalidBase64Url) {
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  for (const auto i : {std::size_t{0}, token.find('.') + 1, token.rfind('.') + 1}) {
    auto invalid = token;
    invalid[i] = '+';
    EXPECT_FALSE(verifier.verify(invalid).has_value());
  }
}

TEST_F(JwtVerifierTest, RejectsOversizedSignedToken) {
  claims["extra"] = std::string(4096, 'a');
  ASSERT_NO_FATAL_FAILURE(test_jwt::sign(claims, token));
  EXPECT_FALSE(verifier.verify(token).has_value());
}

}  // namespace
