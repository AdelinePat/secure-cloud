#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace messaging::domain {

struct Attachment {
  std::string file_id;
  std::int64_t size_bytes = 0;
  std::string checksum_sha256;  // hex, empty when not supplied
};

struct Message {
  std::string id;                       // client-generated (per Message API spec)
  std::string conversation_id;
  std::optional<std::string> user_id;   // sender; nullopt if the user was deleted
  std::vector<unsigned char> ciphertext;
  std::string nonce;                    // base64, opaque to the server
  int16_t encryption_version = 1;
  std::string client_timestamp;         // ISO8601, supplied by the client
  std::string server_timestamp;         // ISO8601, populated by the store
  std::vector<Attachment> attachments;
};

}  // namespace messaging::domain
