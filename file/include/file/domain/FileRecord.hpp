#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace file::domain {

// Metadata of one end-to-end encrypted file. The server only knows sizes and
// who may access it; name, type and key live in the encrypted chat message.
struct FileRecord {
  std::string id;                       // empty when passed to create()
  std::string conversation_id;
  std::optional<std::string> user_id;   // uploader
  std::string status = "UPLOADING";     // "UPLOADING" | "AVAILABLE" | "FAILED" | "DELETED"
  int64_t size_bytes = 0;               // total ciphertext size
  int32_t chunk_count = 0;
  std::string encryption_algorithm = "xchacha20poly1305-secretstream";
  std::string created_at;               // ISO8601, populated by the store
  std::optional<std::string> completed_at;
};

}  // namespace file::domain
