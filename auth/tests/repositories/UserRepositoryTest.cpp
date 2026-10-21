#include <gtest/gtest.h>

#include "auth/domain/User.hpp"
#include "repositories/InMemoryUserRepository.hpp"

// This whole test file never touches Postgres — it's here to demonstrate the
// pattern: AuthService-level logic should be written against IUserRepository&
// and tested exactly like this, with InMemoryUserRepository standing in for
// PostgresUserRepository. PostgresUserRepository itself would get a *separate*
// integration test that runs against a real (test) database, kept out of the
// default unit-test target so `ctest -R auth_tests` stays fast and DB-free.

using auth::domain::User;
using auth::repositories::test::InMemoryUserRepository;

TEST(UserRepository, CreateThenFindById) {
  InMemoryUserRepository repo;

  User input;
  input.username = "alice";
  input.email = "alice@example.com";
  input.password_hash = "$argon2id$...";
  input.public_key = "pk-alice";
  input.key_algorithm = "X25519";

  User created = repo.create(input);
  ASSERT_FALSE(created.id.empty());

  auto found = repo.findById(created.id);
  ASSERT_TRUE(found.has_value());
  EXPECT_EQ(found->username, "alice");
}

TEST(UserRepository, FindByUsernameMissingReturnsNullopt) {
  InMemoryUserRepository repo;
  EXPECT_FALSE(repo.findByUsername("nobody").has_value());
}

TEST(UserRepository, UpdateUsernameOnMissingUserReturnsFalse) {
  InMemoryUserRepository repo;
  EXPECT_FALSE(repo.updateUsername("does-not-exist", "new-name"));
}

TEST(UserRepository, RemoveReflectsInFindById) {
  InMemoryUserRepository repo;

  User input;
  input.username = "bob";
  input.email = "bob@example.com";
  input.password_hash = "$argon2id$...";
  input.public_key = "pk-bob";
  input.key_algorithm = "X25519";

  User created = repo.create(input);
  ASSERT_TRUE(repo.remove(created.id));
  EXPECT_FALSE(repo.findById(created.id).has_value());
}
