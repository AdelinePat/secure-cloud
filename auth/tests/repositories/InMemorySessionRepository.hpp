#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
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

  std::optional<domain::Session> rotate(
      const std::string& currentRefreshTokenHash,
      const domain::Session& newSession) override {
    for (auto& [id, session] : sessionsById_) {
      // Find the session associated with the presented token hash.
      if (session.refresh_token_hash != currentRefreshTokenHash) {
        continue;
      }

      // Reject revoked sessions.
      if (session.revoked_at.has_value()) {
        return std::nullopt;
      }

      // Ensure the replacement belongs to the same user.
      if (session.user_id != newSession.user_id) {
        return std::nullopt;
      }

      // Reject expired sessions.
      std::tm expiration = {};
      std::istringstream stream(session.expires_at);
      stream >> std::get_time(&expiration, "%Y-%m-%dT%H:%M:%S");

      if (stream.fail()) {
        return std::nullopt;
      }

      const std::time_t expirationTime = timegm(&expiration);
      const std::time_t now = std::time(nullptr);

      if (expirationTime <= now) {
        return std::nullopt;
      }

      // Keep the current session ID before inserting a new element.
      const std::string currentSessionId = id;

      // Create the replacement session.
      domain::Session created = create(newSession);

      // Revoke the previous session using its ID.
      sessionsById_.at(currentSessionId).revoked_at = "1970-01-01T00:00:00Z";

      return created;
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
