#include <gtest/gtest.h>

#include "messaging/domain/Errors.hpp"
#include "messaging/services/MessagingService.hpp"
#include "repositories/InMemoryConversationRepository.hpp"
#include "repositories/InMemoryMessageRepository.hpp"
#include "repositories/InMemoryUserDirectory.hpp"

namespace {

namespace domain = messaging::domain;

using messaging::domain::MessagingError;
using messaging::repositories::test::InMemoryConversationRepository;
using messaging::repositories::test::InMemoryMessageRepository;
using messaging::repositories::test::InMemoryUserDirectory;
using messaging::services::CreateConversationInput;
using messaging::services::CreateMessageInput;
using messaging::services::MessagingService;

constexpr const char* kAlice = "11111111-1111-1111-1111-111111111111";
constexpr const char* kBob = "22222222-2222-2222-2222-222222222222";
constexpr const char* kCharlie = "33333333-3333-3333-3333-333333333333";

constexpr const char* kMessageOne = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* kMessageTwo = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";

// A 24-byte nonce, base64-encoded (crypto_secretbox_NONCEBYTES).
constexpr const char* kNonce = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB";

struct Fixture {
  InMemoryConversationRepository conversations;
  InMemoryMessageRepository messages;
  InMemoryUserDirectory users;
  MessagingService service{conversations, messages, users};

  Fixture() {
    users.add(kAlice);
    users.add(kBob);
    users.add(kCharlie);
  }

  domain::Conversation createDirect() {
    CreateConversationInput input;
    input.type = "direct";
    input.participantIds = {kBob};
    return service.createConversation(kAlice, input);
  }
};

CreateMessageInput messageInput(const std::string& id) {
  CreateMessageInput input;
  input.messageId = id;
  input.ciphertext = {1, 2, 3, 4};
  input.nonce = kNonce;
  input.encryptionVersion = 1;
  input.clientTimestamp = "2026-09-02T14:32:00Z";
  return input;
}

}  // namespace

TEST(MessagingServiceTest, CreatesDirectConversationWithAdminAndMember) {
  Fixture fixture;

  const auto conversation = fixture.createDirect();

  ASSERT_EQ(conversation.participants.size(), 2u);
  EXPECT_EQ(conversation.type, "direct");
  EXPECT_FALSE(conversation.name.has_value());

  const auto aliceRole = fixture.conversations.findRole(conversation.id, kAlice);
  const auto bobRole = fixture.conversations.findRole(conversation.id, kBob);

  ASSERT_TRUE(aliceRole.has_value());
  ASSERT_TRUE(bobRole.has_value());
  EXPECT_EQ(*aliceRole, "admin");
  EXPECT_EQ(*bobRole, "member");
}

TEST(MessagingServiceTest, RejectsDuplicateDirectConversation) {
  Fixture fixture;
  fixture.createDirect();

  CreateConversationInput input;
  input.type = "direct";
  input.participantIds = {kBob};

  try {
    fixture.service.createConversation(kAlice, input);
    FAIL() << "expected a 409";
  } catch (const MessagingError& error) {
    EXPECT_EQ(error.status(), 409);
  }
}

TEST(MessagingServiceTest, GroupConversationRequiresName) {
  Fixture fixture;

  CreateConversationInput input;
  input.type = "group";
  input.participantIds = {kBob};

  try {
    fixture.service.createConversation(kAlice, input);
    FAIL() << "expected a 422";
  } catch (const MessagingError& error) {
    EXPECT_EQ(error.status(), 422);
  }
}

TEST(MessagingServiceTest, RejectsUnknownParticipant) {
  Fixture fixture;

  CreateConversationInput input;
  input.type = "direct";
  input.participantIds = {"99999999-9999-4999-8999-999999999999"};

  try {
    fixture.service.createConversation(kAlice, input);
    FAIL() << "expected a 404";
  } catch (const MessagingError& error) {
    EXPECT_EQ(error.status(), 404);
  }
}

TEST(MessagingServiceTest, NonParticipantCannotReadConversation) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  try {
    fixture.service.getConversation(kCharlie, conversation.id);
    FAIL() << "expected a 403";
  } catch (const MessagingError& error) {
    EXPECT_EQ(error.status(), 403);
  }
}

TEST(MessagingServiceTest, StoresMessageAndReturnsServerTimestamp) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const auto result = fixture.service.sendMessage(
      kAlice, conversation.id, messageInput(kMessageOne));

  EXPECT_EQ(result.messageId, kMessageOne);
  EXPECT_EQ(result.status, "SENT");
  EXPECT_FALSE(result.serverTimestamp.empty());

  const auto page =
      fixture.service.history(kBob, conversation.id, std::nullopt, 50);
  ASSERT_EQ(page.messages.size(), 1u);
  EXPECT_EQ(page.messages[0].id, kMessageOne);
  EXPECT_EQ(page.messages[0].user_id, kAlice);
}

TEST(MessagingServiceTest, RejectsDuplicateMessageId) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  fixture.service.sendMessage(kAlice, conversation.id,
                              messageInput(kMessageOne));

  try {
    fixture.service.sendMessage(kAlice, conversation.id,
                                messageInput(kMessageOne));
    FAIL() << "expected a 409";
  } catch (const MessagingError& error) {
    EXPECT_EQ(error.status(), 409);
  }
}

TEST(MessagingServiceTest, HistoryIsNewestFirstAndSupportsPagination) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  fixture.service.sendMessage(kAlice, conversation.id,
                              messageInput(kMessageOne));
  fixture.service.sendMessage(kBob, conversation.id,
                              messageInput(kMessageTwo));

  const auto firstPage =
      fixture.service.history(kAlice, conversation.id, std::nullopt, 1);
  ASSERT_EQ(firstPage.messages.size(), 1u);
  EXPECT_EQ(firstPage.messages[0].id, kMessageTwo);
  ASSERT_TRUE(firstPage.nextCursor.has_value());

  const auto secondPage = fixture.service.history(
      kAlice, conversation.id, firstPage.nextCursor, 1);
  ASSERT_EQ(secondPage.messages.size(), 1u);
  EXPECT_EQ(secondPage.messages[0].id, kMessageOne);
}

TEST(MessagingServiceTest, LeavingLastParticipantDeletesConversation) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const auto result = fixture.service.leaveConversation(kBob, conversation.id);
  EXPECT_EQ(result.status, "left");

  const auto aliceResult =
      fixture.service.leaveConversation(kAlice, conversation.id);
  EXPECT_EQ(aliceResult.status, "deleted");
  EXPECT_FALSE(fixture.conversations.findById(conversation.id).has_value());
}

TEST(MessagingServiceTest, LeavingAdminPromotesAnotherParticipant) {
  Fixture fixture;

  CreateConversationInput input;
  input.type = "group";
  input.name = "Mission Team Alpha";
  input.participantIds = {kBob, kCharlie};
  const auto conversation = fixture.service.createConversation(kAlice, input);

  fixture.service.leaveConversation(kAlice, conversation.id);

  const auto bobRole = fixture.conversations.findRole(conversation.id, kBob);
  ASSERT_TRUE(bobRole.has_value());
  EXPECT_EQ(*bobRole, "admin");
}

TEST(MessagingServiceTest, OnlyAdminCanRemoveParticipant) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  try {
    fixture.service.removeParticipant(kBob, conversation.id, kAlice);
    FAIL() << "expected a 403";
  } catch (const MessagingError& error) {
    EXPECT_EQ(error.status(), 403);
  }

  const auto result =
      fixture.service.removeParticipant(kAlice, conversation.id, kBob);
  EXPECT_EQ(result.status, "LEFT");
  EXPECT_EQ(fixture.conversations.countParticipants(conversation.id), 1u);
}
