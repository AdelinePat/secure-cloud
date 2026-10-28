#pragma once

#include <algorithm>
#include <iomanip>
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

  std::optional<domain::Conversation> findDirectBetween(
      const std::string& firstUserId,
      const std::string& secondUserId) override {
    for (const auto& [id, conv] : conversationsById_) {
      if (conv.type != "direct") continue;

      const bool hasFirst = std::any_of(
          conv.participants.begin(), conv.participants.end(),
          [&](const domain::Participant& p) { return p.user_id == firstUserId; });
      const bool hasSecond = std::any_of(
          conv.participants.begin(), conv.participants.end(),
          [&](const domain::Participant& p) { return p.user_id == secondUserId; });

      if (hasFirst && hasSecond) return conv;
    }
    return std::nullopt;
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

  std::size_t countParticipants(const std::string& conversationId) override {
    auto it = conversationsById_.find(conversationId);
    if (it == conversationsById_.end()) return 0;
    return it->second.participants.size();
  }

  bool promoteToAdmin(const std::string& conversationId,
                      const std::string& userId) override {
    auto it = conversationsById_.find(conversationId);
    if (it == conversationsById_.end()) return false;

    for (auto& participant : it->second.participants) {
      if (participant.user_id == userId) {
        participant.role = "admin";
        return true;
      }
    }
    return false;
  }

  void touch(const std::string& conversationId) override {
    auto it = conversationsById_.find(conversationId);
    if (it == conversationsById_.end()) return;
    it->second.updated_at = "1970-01-01T00:00:01Z";
  }

  void markRead(const std::string&, const std::string&) override {}

 private:
  std::unordered_map<std::string, domain::Conversation> conversationsById_;

  static std::string generateId() {
    static int counter = 0;
    std::ostringstream oss;
    oss << "00000000-0000-4000-8000-" << std::setw(12) << std::setfill('0')
        << counter++;
    return oss.str();
  }
};

}  // namespace messaging::repositories::test
