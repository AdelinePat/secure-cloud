#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace file::domain {

struct FileRecord {
  std::string id;                       // empty when passed to create()
  std::optional<std::string> user_id;
  std::string filename;
  std::optional<std::string> mime_type;
  std::string status = "UPLOADING";     // "UPLOADING" | "AVAILABLE" | "FAILED" | "DELETED"
  int64_t size_bytes = 0;
  std::string checksum;                 // sha256 of the ciphertext
  std::string storage_ref;              // MinIO/Garage object key
  std::string encryption_algorithm = "AES-256-GCM";
  std::string nonce;
  std::string created_at;               // ISO8601, populated by the store
};

}  // namespace file::domain
