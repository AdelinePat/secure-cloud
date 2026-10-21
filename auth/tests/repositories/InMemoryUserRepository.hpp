#pragma once

#include <random>
#include <sstream>
#include <unordered_map>

#include "auth/repositories/IUserRepository.hpp"

namespace auth::repositories::test {

// Test double for IUserRepository. Not thread-safe; intended for unit tests
// only — lets AuthService logic be tested with no live Postgres.
class InMemoryUserRepository : public IUserRepository {
 public:
  domain::User create(const domain::User& user) override {
    domain::User stored = user;
    stored.id = generateId();
    stored.created_at = "1970-01-01T00:00:00Z";
    stored.updated_at = stored.created_at;
    usersById_[stored.id] = stored;
    return stored;
  }

  std::optional<domain::User> findById(const std::string& id) override {
    auto it = usersById_.find(id);
    if (it == usersById_.end()) return std::nullopt;
    return it->second;
  }

  std::optional<domain::User> findByUsername(
      const std::string& username) override {
    for (const auto& [id, u] : usersById_) {
      if (u.username == username) return u;
    }
    return std::nullopt;
  }

  bool updateUsername(const std::string& id,
                       const std::string& newUsername) override {
    auto it = usersById_.find(id);
    if (it == usersById_.end()) return false;
    it->second.username = newUsername;
    return true;
  }

  bool updatePasswordHash(const std::string& id,
                           const std::string& newPasswordHash) override {
    auto it = usersById_.find(id);
    if (it == usersById_.end()) return false;
    it->second.password_hash = newPasswordHash;
    return true;
  }

  bool remove(const std::string& id) override {
    return usersById_.erase(id) > 0;
  }

  // Test helper: seed a user with a known id, bypassing create()'s id generation.
  void seed(const domain::User& user) { usersById_[user.id] = user; }

 private:
  std::unordered_map<std::string, domain::User> usersById_;

  static std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng();
    return oss.str();
  }
};

}  // namespace auth::repositories::test
