
#pragma once

#include <string>

namespace auth::crypto {

class PasswordHasher {
 public:
  // Hash a plaintext password using Argon2id.
  static std::string hash(const std::string& password);

  // Verify a plaintext password against a stored Argon2id hash.
  static bool verify(const std::string& password,
                     const std::string& storedHash);
};

}  // namespace auth::crypto
