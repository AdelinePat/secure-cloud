#pragma once

#include <pqxx/pqxx>

#include "messaging/repositories/IConversationRepository.hpp"

namespace messaging::repositories {

class PostgresConversationRepository : public IConversationRepository {
 public:
  explicit PostgresConversationRepository(pqxx::connection& conn);

  domain::Conversation create(
      const domain::Conversation& conversation) override;
  std::optional<domain::Conversation> findById(const std::string& id) override;
  std::vector<domain::Conversation> listForUser(
      const std::string& userId) override;
  std::optional<domain::Conversation> findDirectBetween(
      const std::string& firstUserId,
      const std::string& secondUserId) override;
  bool remove(const std::string& id) override;
  bool addParticipant(const std::string& conversationId,
                       const std::string& userId,
                       const std::string& role) override;
  bool removeParticipant(const std::string& conversationId,
                          const std::string& userId) override;
  std::optional<std::string> findRole(const std::string& conversationId,
                                       const std::string& userId) override;
  std::size_t countParticipants(const std::string& conversationId) override;
  bool promoteToAdmin(const std::string& conversationId,
                      const std::string& userId) override;
  void touch(const std::string& conversationId) override;
  void markRead(const std::string& conversationId,
                const std::string& userId) override;

 private:
  pqxx::connection& conn_;
  std::vector<domain::Participant> fetchParticipants(
      pqxx::work& txn, const std::string& conversationId);
  domain::Conversation readConversation(const pqxx::row& row);
};

}  // namespace messaging::repositories
