#pragma once

#include <map>
#include <string>

#include "messaging/repositories/IUserDirectory.hpp"

namespace messaging::repositories::test {

class InMemoryUserDirectory : public IUserDirectory {
 public:
  void addUser(const std::string& id, const std::string& username,
               const std::string& publicKey = "") {
    usernames_[username] = id;
    keys_[id] = publicKey;
  }

  std::optional<std::string> findUserIdByUsername(
      const std::string& username) override {
    auto it = usernames_.find(username);
    if (it == usernames_.end()) return std::nullopt;
    return it->second;
  }

  bool publishIdentityKey(const std::string& userId,
                          const std::string& publicKey) override {
    auto it = keys_.find(userId);
    if (it == keys_.end()) return false;
    if (!it->second.empty() && it->second != publicKey) return false;
    it->second = publicKey;
    return true;
  }

  std::optional<std::string> findIdentityKey(
      const std::string& userId) override {
    auto it = keys_.find(userId);
    if (it == keys_.end() || it->second.empty()) return std::nullopt;
    return it->second;
  }

 private:
  std::map<std::string, std::string> usernames_;  // username -> id
  std::map<std::string, std::string> keys_;       // id -> public key
};

}  // namespace messaging::repositories::test
