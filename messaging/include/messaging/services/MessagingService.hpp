#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "messaging/domain/Conversation.hpp"
#include "messaging/domain/Message.hpp"
#include "messaging/repositories/IConversationRepository.hpp"
#include "messaging/repositories/IMessageRepository.hpp"
#include "messaging/repositories/IUserDirectory.hpp"

namespace messaging::services {

struct CreateConversationInput {
  std::string type;
  std::optional<std::string> name;
  std::vector<std::string> participantIds;
};

struct CreateMessageInput {
  std::string messageId;
  std::vector<unsigned char> ciphertext;
  std::string nonce;  // base64, opaque to the server
  int encryptionVersion = 1;
  std::string clientTimestamp;
  std::vector<domain::Attachment> attachments;
};

struct SendMessageResult {
  std::string messageId;
  std::string status;  // "SENT"
  std::string serverTimestamp;
};

struct LeaveResult {
  std::string conversationId;
  std::string status;  // "left" | "deleted"
};

class MessagingService {
 public:
  MessagingService(repositories::IConversationRepository& conversationRepository,
                   repositories::IMessageRepository& messageRepository,
                   repositories::IUserDirectory& userDirectory);

  domain::Conversation createConversation(
      const std::string& userId, const CreateConversationInput& input);

  std::vector<domain::Conversation> listConversations(
      const std::string& userId);

  domain::Conversation getConversation(const std::string& userId,
                                       const std::string& conversationId);

  void deleteConversation(const std::string& userId,
                          const std::string& conversationId);

  LeaveResult leaveConversation(const std::string& userId,
                                const std::string& conversationId);

  LeaveResult removeParticipant(const std::string& callerId,
                                const std::string& conversationId,
                                const std::string& targetUserId);

  repositories::MessagePage history(const std::string& userId,
                                    const std::string& conversationId,
                                    std::optional<std::string> before,
                                    int limit);

  SendMessageResult sendMessage(const std::string& userId,
                                const std::string& conversationId,
                                const CreateMessageInput& input);

 private:
  // Throws MessagingError(404/403) when the conversation is missing or the
  // caller is not a participant. Returns the caller's role on success.
  std::string requireParticipant(const std::string& userId,
                                 const std::string& conversationId,
                                 domain::Conversation& conversation);

  repositories::IConversationRepository& conversationRepository_;
  repositories::IMessageRepository& messageRepository_;
  repositories::IUserDirectory& userDirectory_;
};

}  // namespace messaging::services
