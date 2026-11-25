#pragma once

#include <cstdint>
#include <string>

namespace file::domain {

// Bookkeeping for one stored ciphertext chunk; the bytes themselves are on
// disk, not in the database.
struct FileChunk {
  std::string file_id;
  int32_t index = 0;
  int32_t size_bytes = 0;
  std::string sha256;  // lowercase hex digest of the stored bytes
};

}  // namespace file::domain
