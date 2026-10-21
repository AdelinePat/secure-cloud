#pragma once

#include <random>
#include <sstream>
#include <unordered_map>

#include "auth/repositories/ISessionRepository.hpp"

namespace auth::repositories::test {

class InMemorySessionRepository : public ISessionRepository {
 public:
  domain::Session create(const domain::Session& session) override {
    domain::Session stored = session;
    stored.id = generateId();
    stored.issued_at = "1970-01-01T00:00:00Z";
    sessionsById_[stored.id] = stored;
    return stored;
  }

  std::optional<domain::Session> findByRefreshTokenHash(
      const std::string& hash) override {
    for (const auto& [id, s] : sessionsById_) {
      if (s.refresh_token_hash == hash) return s;
    }
    return std::nullopt;
  }

  bool revoke(const std::string& sessionId) override {
    auto it = sessionsById_.find(sessionId);
    if (it == sessionsById_.end() || it->second.revoked_at.has_value())
      return false;
    it->second.revoked_at = "1970-01-01T00:00:00Z";
    return true;
  }

  int revokeAllForUser(const std::string& userId) override {
    int count = 0;
    for (auto& [id, s] : sessionsById_) {
      if (s.user_id == userId && !s.revoked_at.has_value()) {
        s.revoked_at = "1970-01-01T00:00:00Z";
        ++count;
      }
    }
    return count;
  }

 private:
  std::unordered_map<std::string, domain::Session> sessionsById_;

  static std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng();
    return oss.str();
  }
};

}  // namespace auth::repositories::test
