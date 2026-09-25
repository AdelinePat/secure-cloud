#pragma once

#include <algorithm>
#include <random>
#include <sstream>
#include <unordered_map>

#include "messaging/repositories/IConversationRepository.hpp"

namespace messaging::repositories::test {

class InMemoryConversationRepository : public IConversationRepository {
 public:
  domain::Conversation create(const domain::Conversation& conversation) override {
    domain::Conversation stored = conversation;
    stored.id = generateId();
    stored.created_at = "1970-01-01T00:00:00Z";
    stored.updated_at = stored.created_at;
    conversationsById_[stored.id] = stored;
    return stored;
  }

  std::optional<domain::Conversation> findById(const std::string& id) override {
    auto it = conversationsById_.find(id);
    if (it == conversationsById_.end()) return std::nullopt;
    return it->second;
  }

  std::vector<domain::Conversation> listForUser(const std::string& userId) override {
    std::vector<domain::Conversation> result;
    for (const auto& [id, conv] : conversationsById_) {
      bool isParticipant = std::any_of(
          conv.participants.begin(), conv.participants.end(),
          [&](const domain::Participant& p) { return p.user_id == userId; });
      if (isParticipant) result.push_back(conv);
    }
    return result;
  }

  bool remove(const std::string& id) override {
    return conversationsById_.erase(id) > 0;
  }

  bool addParticipant(const std::string& conversationId,
                       const std::string& userId,
                       const std::string& role) override {
    auto it = conversationsById_.find(conversationId);
    if (it == conversationsById_.end()) return false;
    it->second.participants.push_back(
        domain::Participant{userId, role, "1970-01-01T00:00:00Z"});
    return true;
  }

  bool removeParticipant(const std::string& conversationId,
                          const std::string& userId) override {
    auto it = conversationsById_.find(conversationId);
    if (it == conversationsById_.end()) return false;
    auto& participants = it->second.participants;
    auto before = participants.size();
    participants.erase(
        std::remove_if(participants.begin(), participants.end(),
                        [&](const domain::Participant& p) {
                          return p.user_id == userId;
                        }),
        participants.end());
    return participants.size() < before;
  }

  std::optional<std::string> findRole(const std::string& conversationId,
                                       const std::string& userId) override {
    auto it = conversationsById_.find(conversationId);
    if (it == conversationsById_.end()) return std::nullopt;
    for (const auto& p : it->second.participants) {
      if (p.user_id == userId) return p.role;
    }
    return std::nullopt;
  }

 private:
  std::unordered_map<std::string, domain::Conversation> conversationsById_;

  static std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng();
    return oss.str();
  }
};

}  // namespace messaging::repositories::test
