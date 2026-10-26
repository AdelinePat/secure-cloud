
#include <gtest/gtest.h>
#include <sodium.h>

#include "auth/crypto/PasswordHasher.hpp"

namespace {

class PasswordHasherTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_GE(sodium_init(), 0); }
};

TEST_F(PasswordHasherTest, CorrectPasswordIsAccepted) {
  const std::string password = "SecureChat_Test_123!";
  const std::string hash = auth::crypto::PasswordHasher::hash(password);

  EXPECT_TRUE(auth::crypto::PasswordHasher::verify(password, hash));
}

TEST_F(PasswordHasherTest, IncorrectPasswordIsRejected) {
  const std::string hash =
      auth::crypto::PasswordHasher::hash("CorrectPassword123!");

  EXPECT_FALSE(auth::crypto::PasswordHasher::verify("WrongPassword123!", hash));
}

}  // namespace
