#pragma once

#include <optional>
#include <string>
#include <vector>

#include "messaging/domain/Conversation.hpp"

namespace messaging::repositories {

class IConversationRepository {
 public:
  virtual ~IConversationRepository() = default;

  // Creates the conversation row and every row in `conversation.participants`
  // atomically (single transaction) — including the creator, who must be
  // present in that list with role "admin".
  virtual domain::Conversation create(
      const domain::Conversation& conversation) = 0;

  // Conversation with its participants populated, or nullopt if not found.
  virtual std::optional<domain::Conversation> findById(
      const std::string& id) = 0;

  // Every conversation the given user participates in, participants populated.
  virtual std::vector<domain::Conversation> listForUser(
      const std::string& userId) = 0;

  virtual bool remove(const std::string& id) = 0;

  virtual bool addParticipant(const std::string& conversationId,
                               const std::string& userId,
                               const std::string& role) = 0;
  virtual bool removeParticipant(const std::string& conversationId,
                                  const std::string& userId) = 0;

  // nullopt if the user isn't a participant of this conversation.
  virtual std::optional<std::string> findRole(
      const std::string& conversationId, const std::string& userId) = 0;
};

}  // namespace messaging::repositories
