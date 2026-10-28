#include "messaging/services/MessagingService.hpp"

#include <algorithm>
#include <unordered_set>

#include <sodium.h>

#include "messaging/domain/Errors.hpp"
#include "messaging/util/Base64.hpp"
#include "messaging/util/Uuid.hpp"

namespace messaging::services {

namespace {

constexpr int kMaxGroupParticipants = 100;
constexpr int kDefaultHistoryLimit = 50;
constexpr int kMaxHistoryLimit = 100;

domain::MessagingError badRequest(const std::string& message) {
  return domain::MessagingError(400, "BAD_REQUEST", message);
}

domain::MessagingError notFound(const std::string& message) {
  return domain::MessagingError(404, "NOT_FOUND", message);
}

domain::MessagingError forbidden(const std::string& message) {
  return domain::MessagingError(403, "FORBIDDEN", message);
}

domain::MessagingError conflict(const std::string& message) {
  return domain::MessagingError(409, "CONFLICT", message);
}

void requireConversationId(const std::string& conversationId) {
  if (!util::isUuid(conversationId)) {
    throw notFound("Conversation not found");
  }
}

}  // namespace

MessagingService::MessagingService(
    repositories::IConversationRepository& conversationRepository,
    repositories::IMessageRepository& messageRepository,
    repositories::IUserDirectory& userDirectory)
    : conversationRepository_(conversationRepository),
      messageRepository_(messageRepository),
      userDirectory_(userDirectory) {}

domain::Conversation MessagingService::createConversation(
    const std::string& userId, const CreateConversationInput& input) {
  if (input.type != "direct" && input.type != "group") {
    throw badRequest("type must be 'direct' or 'group'");
  }

  if (input.participantIds.empty()) {
    throw badRequest("participant_ids must not be empty");
  }

  if (input.type == "direct" && input.participantIds.size() != 1) {
    throw badRequest("a direct conversation requires exactly one participant");
  }

  if (input.type == "group") {
    if (!input.name.has_value() || input.name->empty()) {
      throw domain::MessagingError(422, "UNPROCESSABLE_ENTITY",
                                   "name is required for a group conversation");
    }
    if (static_cast<int>(input.participantIds.size()) + 1 >
        kMaxGroupParticipants) {
      throw badRequest("a group conversation cannot exceed 100 participants");
    }
  }

  // Deduplicate and drop the creator if it was included by mistake.
  std::vector<std::string> participantIds;
  std::unordered_set<std::string> seen{userId};
  for (const auto& id : input.participantIds) {
    if (seen.insert(id).second) participantIds.push_back(id);
  }

  if (participantIds.empty()) {
    throw badRequest("participant_ids must contain another user");
  }

  for (const auto& participantId : participantIds) {
    if (!util::isUuid(participantId) || !userDirectory_.exists(participantId)) {
      throw notFound("A participant does not exist");
    }
  }

  if (input.type == "direct") {
    if (conversationRepository_.findDirectBetween(userId, participantIds[0])
            .has_value()) {
      throw conflict("A direct conversation between these users already exists");
    }
  }

  domain::Conversation conversation;
  conversation.creator_id = userId;
  conversation.type = input.type;
  if (input.type == "group") conversation.name = input.name;

  conversation.participants.push_back(
      domain::Participant{userId, "admin", ""});
  for (const auto& participantId : participantIds) {
    conversation.participants.push_back(
        domain::Participant{participantId, "member", ""});
  }

  return conversationRepository_.create(conversation);
}

std::vector<domain::Conversation> MessagingService::listConversations(
    const std::string& userId) {
  return conversationRepository_.listForUser(userId);
}

std::string MessagingService::requireParticipant(
    const std::string& userId, const std::string& conversationId,
    domain::Conversation& conversation) {
  requireConversationId(conversationId);

  auto stored = conversationRepository_.findById(conversationId);
  if (!stored.has_value()) {
    throw notFound("Conversation not found");
  }

  const auto role = conversationRepository_.findRole(conversationId, userId);
  if (!role.has_value()) {
    throw forbidden("Caller is not a participant of this conversation");
  }

  conversation = std::move(*stored);
  return *role;
}

domain::Conversation MessagingService::getConversation(
    const std::string& userId, const std::string& conversationId) {
  domain::Conversation conversation;
  requireParticipant(userId, conversationId, conversation);
  return conversation;
}

void MessagingService::deleteConversation(const std::string& userId,
                                          const std::string& conversationId) {
  domain::Conversation conversation;
  const std::string role =
      requireParticipant(userId, conversationId, conversation);

  if (role != "admin") {
    throw forbidden("Only a conversation admin can delete it");
  }

  conversationRepository_.remove(conversationId);
}

LeaveResult MessagingService::leaveConversation(
    const std::string& userId, const std::string& conversationId) {
  domain::Conversation conversation;
  const std::string role =
      requireParticipant(userId, conversationId, conversation);

  if (conversationRepository_.countParticipants(conversationId) <= 1) {
    conversationRepository_.remove(conversationId);
    return LeaveResult{conversationId, "deleted"};
  }

  conversationRepository_.removeParticipant(conversationId, userId);

  // A group must keep an admin. Promote the longest-standing remaining member
  // if the departing user was the last admin.
  if (role == "admin") {
    const bool hasAdmin = std::any_of(
        conversation.participants.begin(), conversation.participants.end(),
        [&](const domain::Participant& participant) {
          return participant.user_id != userId && participant.role == "admin";
        });

    if (!hasAdmin) {
      for (const auto& participant : conversation.participants) {
        if (participant.user_id != userId) {
          conversationRepository_.promoteToAdmin(conversationId,
                                                 participant.user_id);
          break;
        }
      }
    }
  }

  return LeaveResult{conversationId, "left"};
}

LeaveResult MessagingService::removeParticipant(
    const std::string& callerId, const std::string& conversationId,
    const std::string& targetUserId) {
  domain::Conversation conversation;
  const std::string callerRole =
      requireParticipant(callerId, conversationId, conversation);

  if (callerRole != "admin") {
    throw forbidden("Only a conversation admin can remove a participant");
  }

  if (callerId == targetUserId) {
    throw badRequest("Use the leave endpoint to remove yourself");
  }

  const auto targetRole =
      conversationRepository_.findRole(conversationId, targetUserId);
  if (!targetRole.has_value()) {
    throw notFound("Participant not found");
  }

  conversationRepository_.removeParticipant(conversationId, targetUserId);
  return LeaveResult{conversationId, "LEFT"};
}

repositories::MessagePage MessagingService::history(
    const std::string& userId, const std::string& conversationId,
    std::optional<std::string> before, int limit) {
  domain::Conversation conversation;
  requireParticipant(userId, conversationId, conversation);

  if (limit <= 0) limit = kDefaultHistoryLimit;
  if (limit > kMaxHistoryLimit) limit = kMaxHistoryLimit;

  auto page = messageRepository_.history(conversationId, std::move(before),
                                         limit);

  // Fetching history is the client telling us it has seen these messages.
  conversationRepository_.markRead(conversationId, userId);

  return page;
}

SendMessageResult MessagingService::sendMessage(
    const std::string& userId, const std::string& conversationId,
    const CreateMessageInput& input) {
  domain::Conversation conversation;
  requireParticipant(userId, conversationId, conversation);

  if (!util::isUuid(input.messageId)) {
    throw badRequest("message_id must be a UUID");
  }

  if (input.ciphertext.empty()) {
    throw badRequest("ciphertext must not be empty");
  }

  if (input.nonce.empty()) {
    throw badRequest("nonce must not be empty");
  }

  std::vector<unsigned char> nonce;
  try {
    nonce = util::base64Decode(input.nonce);
  } catch (const std::invalid_argument&) {
    throw badRequest("nonce must be base64");
  }

  if (input.encryptionVersion == 1 &&
      nonce.size() != crypto_secretbox_NONCEBYTES) {
    throw badRequest("nonce has an invalid size for encryption_version 1");
  }

  for (const auto& attachment : input.attachments) {
    if (!util::isUuid(attachment.file_id)) {
      throw badRequest("attachment file_id must be a UUID");
    }
    if (attachment.size_bytes < 0) {
      throw badRequest("attachment size_bytes must be positive");
    }
  }

  domain::Message message;
  message.id = input.messageId;
  message.conversation_id = conversationId;
  message.user_id = userId;
  message.ciphertext = input.ciphertext;
  message.nonce = input.nonce;
  message.encryption_version =
      static_cast<int16_t>(input.encryptionVersion);
  message.client_timestamp = input.clientTimestamp;
  message.attachments = input.attachments;

  try {
    const domain::Message stored = messageRepository_.create(message);
    conversationRepository_.touch(conversationId);

    return SendMessageResult{stored.id, "SENT", stored.server_timestamp};

  } catch (const repositories::DuplicateMessageError&) {
    throw conflict("Duplicate message_id");
  }
}

}  // namespace messaging::services
