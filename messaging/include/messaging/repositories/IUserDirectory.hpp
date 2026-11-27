#pragma once

#include <optional>
#include <string>

namespace messaging::repositories {

// The parts of the users table messaging reads or writes. The table belongs
// to auth; keeping every access behind this interface makes it possible to
// move these calls to an auth API later without touching the routes.
class IUserDirectory {
 public:
  virtual ~IUserDirectory() = default;

  virtual std::optional<std::string> findUserIdByUsername(
      const std::string& username) = 0;

  // Stores the user's public identity key. The first key is immutable:
  // returns true if the key is now stored (first publication or identical
  // retry), false if a different key is already there.
  virtual bool publishIdentityKey(const std::string& userId,
                                  const std::string& publicKey) = 0;

  // The user's published key, or nullopt if the user is unknown or has not
  // published one yet.
  virtual std::optional<std::string> findIdentityKey(
      const std::string& userId) = 0;
};

}  // namespace messaging::repositories
