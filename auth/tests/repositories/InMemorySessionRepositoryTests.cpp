#include <gtest/gtest.h>

#include "repositories/InMemorySessionRepository.hpp"

namespace auth::repositories::test {

TEST(InMemorySessionRepositoryTests, RotateValidSession) {
  InMemorySessionRepository repository;

  // Create an active session with a future expiration date.
  domain::Session currentSession{};
  currentSession.user_id = "user-123";
  currentSession.refresh_token_hash = "old-token-hash";
  currentSession.expires_at = "2099-01-01T00:00:00Z";

  const domain::Session storedSession = repository.create(currentSession);

  // Prepare a replacement session for the same user.
  domain::Session replacement{};
  replacement.user_id = "user-123";
  replacement.refresh_token_hash = "new-token-hash";
  replacement.expires_at = "2099-01-08T00:00:00Z";

  const auto result = repository.rotate("old-token-hash", replacement);

  ASSERT_TRUE(result.has_value());

  // The replacement must have a different identifier.
  EXPECT_NE(result->id, storedSession.id);

  // The original session must now be revoked.
  const auto previousSession =
      repository.findByRefreshTokenHash("old-token-hash");

  ASSERT_TRUE(previousSession.has_value());
  EXPECT_TRUE(previousSession->revoked_at.has_value());

  // The replacement session must remain active.
  const auto newSession = repository.findByRefreshTokenHash("new-token-hash");

  ASSERT_TRUE(newSession.has_value());
  EXPECT_FALSE(newSession->revoked_at.has_value());

  EXPECT_EQ(newSession->user_id, "user-123");
}

TEST(InMemorySessionRepositoryTests, RejectReusedRefreshToken) {
  InMemorySessionRepository repository;

  // Create an active session.
  domain::Session currentSession{};
  currentSession.user_id = "user-123";
  currentSession.refresh_token_hash = "old-token-hash";
  currentSession.expires_at = "2099-01-01T00:00:00Z";

  repository.create(currentSession);

  // Prepare the first replacement session.
  domain::Session firstReplacement{};
  firstReplacement.user_id = "user-123";
  firstReplacement.refresh_token_hash = "new-token-hash-1";
  firstReplacement.expires_at = "2099-01-08T00:00:00Z";

  // The first rotation must succeed.
  const auto firstResult =
      repository.rotate("old-token-hash", firstReplacement);

  ASSERT_TRUE(firstResult.has_value());

  // Attempt to reuse the original refresh token.
  domain::Session secondReplacement{};
  secondReplacement.user_id = "user-123";
  secondReplacement.refresh_token_hash = "new-token-hash-2";
  secondReplacement.expires_at = "2099-01-08T00:00:00Z";

  const auto secondResult =
      repository.rotate("old-token-hash", secondReplacement);

  // A revoked session must never be rotated again.
  EXPECT_FALSE(secondResult.has_value());

  // The rejected rotation must not create another session.
  const auto unexpectedSession =
      repository.findByRefreshTokenHash("new-token-hash-2");

  EXPECT_FALSE(unexpectedSession.has_value());
}

TEST(InMemorySessionRepositoryTests, RejectExpiredSession) {
  InMemorySessionRepository repository;

  // Create a session that has already expired.
  domain::Session expiredSession{};
  expiredSession.user_id = "user-123";
  expiredSession.refresh_token_hash = "expired-token-hash";
  expiredSession.expires_at = "2020-01-01T00:00:00Z";

  repository.create(expiredSession);

  // Prepare a replacement session.
  domain::Session replacement{};
  replacement.user_id = "user-123";
  replacement.refresh_token_hash = "new-token-hash";
  replacement.expires_at = "2099-01-01T00:00:00Z";

  // Attempt to rotate the expired session.
  const auto result = repository.rotate("expired-token-hash", replacement);

  EXPECT_FALSE(result.has_value());

  // The rejected rotation must not create a replacement session.
  const auto unexpectedSession =
      repository.findByRefreshTokenHash("new-token-hash");

  EXPECT_FALSE(unexpectedSession.has_value());

  // The original session should remain unchanged.
  const auto originalSession =
      repository.findByRefreshTokenHash("expired-token-hash");

  ASSERT_TRUE(originalSession.has_value());
  EXPECT_FALSE(originalSession->revoked_at.has_value());
}

TEST(InMemorySessionRepositoryTests, RejectRevokedSession) {
  InMemorySessionRepository repository;

  // Create a valid session.
  domain::Session currentSession{};
  currentSession.user_id = "user-123";
  currentSession.refresh_token_hash = "revoked-token-hash";
  currentSession.expires_at = "2099-01-01T00:00:00Z";

  const auto storedSession = repository.create(currentSession);

  // Explicitly revoke the session.
  ASSERT_TRUE(repository.revoke(storedSession.id));

  // Prepare a replacement session.
  domain::Session replacement{};
  replacement.user_id = "user-123";
  replacement.refresh_token_hash = "new-token-hash";
  replacement.expires_at = "2099-01-08T00:00:00Z";

  // A revoked session must not be renewed.
  const auto result = repository.rotate("revoked-token-hash", replacement);

  EXPECT_FALSE(result.has_value());

  // No replacement session should have been created.
  EXPECT_FALSE(repository.findByRefreshTokenHash("new-token-hash").has_value());
}

TEST(InMemorySessionRepositoryTests, RejectDifferentUser) {
  InMemorySessionRepository repository;

  // Create an active session belonging to Alice.
  domain::Session aliceSession{};
  aliceSession.user_id = "alice-id";
  aliceSession.refresh_token_hash = "alice-token-hash";
  aliceSession.expires_at = "2099-01-01T00:00:00Z";

  repository.create(aliceSession);

  // Attempt to create a replacement session for Bob.
  domain::Session bobSession{};
  bobSession.user_id = "bob-id";
  bobSession.refresh_token_hash = "bob-token-hash";
  bobSession.expires_at = "2099-01-08T00:00:00Z";

  const auto result = repository.rotate("alice-token-hash", bobSession);

  // Rotation must be rejected.
  EXPECT_FALSE(result.has_value());

  // Bob's session must not have been created.
  EXPECT_FALSE(repository.findByRefreshTokenHash("bob-token-hash").has_value());

  // Alice's original session must remain active.
  const auto originalSession =
      repository.findByRefreshTokenHash("alice-token-hash");

  ASSERT_TRUE(originalSession.has_value());
  EXPECT_FALSE(originalSession->revoked_at.has_value());
}

TEST(InMemorySessionRepositoryTests, RejectUnknownRefreshToken) {
  InMemorySessionRepository repository;

  // Prepare a replacement session.
  domain::Session replacement{};
  replacement.user_id = "user-123";
  replacement.refresh_token_hash = "new-token-hash";
  replacement.expires_at = "2099-01-01T00:00:00Z";

  // Attempt to rotate a session that does not exist.
  const auto result = repository.rotate("unknown-token-hash", replacement);

  // An unknown refresh token must be rejected.
  EXPECT_FALSE(result.has_value());

  // No replacement session should have been created.
  const auto unexpectedSession =
      repository.findByRefreshTokenHash("new-token-hash");

  EXPECT_FALSE(unexpectedSession.has_value());
}

}  // namespace auth::repositories::test