#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>

#include "messaging/repositories/PostgresMessageRepository.hpp"

TEST(PostgresMessageRepositoryTest, ValidatesLimitsAndHandlesEmptyPages) {
  const char* connectionString = std::getenv("SECURECHAT_HISTORY_TEST_DB");
  if (!connectionString) {
    GTEST_SKIP() << "Set SECURECHAT_HISTORY_TEST_DB to run the PostgreSQL test";
  }
  pqxx::connection connection(connectionString);
  {
    pqxx::work txn(connection);
    txn.exec("CREATE TEMP TABLE messages (LIKE public.messages)");
    txn.commit();
  }

  messaging::repositories::PostgresMessageRepository repository(connection);
  const std::string id = "01234567-89ab-4def-8123-456789abcdef";
  for (const int limit : {-1, 0, 101}) {
    SCOPED_TRACE(limit);
    EXPECT_THROW(repository.history(id, std::nullopt, limit), std::invalid_argument);
  }
  const auto empty = repository.history(id, std::nullopt, 100);
  EXPECT_TRUE(empty.messages.empty());
  EXPECT_FALSE(empty.nextCursor);

  {
    pqxx::work txn(connection);
    txn.exec_params(
        "INSERT INTO messages (id, conversation_id, ciphertext, nonce, "
        "encryption_version, server_timestamp, updated_at) "
        "SELECT gen_random_uuid(), $1, decode('aa', 'hex'), 'nonce', 2, "
        "CURRENT_TIMESTAMP, CURRENT_TIMESTAMP FROM generate_series(1, 101)", id);
    txn.commit();
  }

  const auto first = repository.history(id, std::nullopt, 100);
  ASSERT_EQ(first.messages.size(), 100u);
  ASSERT_TRUE(first.nextCursor);
  const auto second = repository.history(id, first.nextCursor, 100);
  ASSERT_EQ(second.messages.size(), 1u);
  EXPECT_NE(first.messages.back().id, second.messages.front().id);
  EXPECT_FALSE(second.nextCursor);
}
