#pragma once

#include <string>

namespace auth::domain {

struct User {
  std::string id;             // empty when constructing a new user to pass to create()
  std::string username;
  std::string email;
  std::string password_hash;  // Argon2id string from crypto_pwhash_str, never plaintext
  std::string public_key;
  std::string key_algorithm;  // e.g. "X25519"
  std::string created_at;     // ISO8601, populated by the store
  std::string updated_at;     // ISO8601, populated by the store
};

}  // namespace auth::domain
