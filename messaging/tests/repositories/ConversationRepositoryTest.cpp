#include <gtest/gtest.h>

#include "messaging/domain/Conversation.hpp"
#include "repositories/InMemoryConversationRepository.hpp"
#include "repositories/InMemoryMessageRepository.hpp"

using messaging::domain::Conversation;
using messaging::domain::Message;
using messaging::domain::Participant;
using messaging::repositories::test::InMemoryConversationRepository;
using messaging::repositories::test::InMemoryMessageRepository;

TEST(ConversationRepository, CreateGroupWithParticipants) {
  InMemoryConversationRepository repo;

  Conversation conv;
  conv.type = "group";
  conv.name = "Mission Team Alpha";
  conv.creator_id = "alice-id";
  conv.participants = {Participant{"alice-id", "admin", ""},
                        Participant{"bob-id", "member", ""}};

  Conversation stored = repo.create(conv);
  ASSERT_FALSE(stored.id.empty());
  EXPECT_EQ(stored.participants.size(), 2u);

  auto role = repo.findRole(stored.id, "bob-id");
  ASSERT_TRUE(role.has_value());
  EXPECT_EQ(*role, "member");
}

TEST(ConversationRepository, ListForUserOnlyReturnsTheirConversations) {
  InMemoryConversationRepository repo;

  Conversation conv;
  conv.type = "direct";
  conv.participants = {Participant{"alice-id", "member", ""},
                        Participant{"bob-id", "member", ""}};
  repo.create(conv);

  EXPECT_EQ(repo.listForUser("alice-id").size(), 1u);
  EXPECT_EQ(repo.listForUser("charlie-id").size(), 0u);
}

TEST(MessageRepository, DuplicateIdThrows) {
  InMemoryMessageRepository repo;

  Message m;
  m.id = "msg-1";
  m.conversation_id = "conv-1";
  m.nonce = "nonce";

  repo.create(m);
  EXPECT_THROW(repo.create(m), std::runtime_error);
}

TEST(MessageRepository, HistoryReturnsNewestFirst) {
  InMemoryMessageRepository repo;

  for (int i = 0; i < 3; ++i) {
    Message m;
    m.id = "msg-" + std::to_string(i);
    m.conversation_id = "conv-1";
    m.nonce = "nonce";
    repo.create(m);
  }

  auto page = repo.history("conv-1", std::nullopt, 10);
  ASSERT_EQ(page.messages.size(), 3u);
  EXPECT_EQ(page.messages.front().id, "msg-2");  // most recently created
}
