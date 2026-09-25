#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace messaging::domain {

struct Message {
  std::string id;                       // client-generated (per Message API spec)
  std::string conversation_id;
  std::optional<std::string> user_id;   // sender; nullopt if the user was deleted
  std::vector<unsigned char> ciphertext;
  std::string nonce;
  int16_t encryption_version = 1;
  std::string server_timestamp;         // ISO8601, populated by the store
};

}  // namespace messaging::domain
