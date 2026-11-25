#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace file::storage {

// Where ciphertext chunk bytes live. The database only records that a chunk
// arrived (see IFileRepository::upsertChunk).
class IChunkStorage {
 public:
  virtual ~IChunkStorage() = default;

  // Stores (or replaces) one chunk. Throws on I/O failure.
  virtual void write(const std::string& fileId, int32_t index,
                     std::string_view bytes) = 0;

  // The chunk's bytes, or nullopt if it was never written.
  virtual std::optional<std::string> read(const std::string& fileId,
                                          int32_t index) = 0;

  // Removes every chunk of a file. Missing files are not an error.
  virtual void removeFile(const std::string& fileId) = 0;
};

// Stores chunks as <root>/<file_id>/<index>. Writes go to a temporary file
// first and are renamed into place, so a crash never leaves a partial chunk.
class FilesystemChunkStorage : public IChunkStorage {
 public:
  explicit FilesystemChunkStorage(std::filesystem::path root);

  void write(const std::string& fileId, int32_t index,
             std::string_view bytes) override;
  std::optional<std::string> read(const std::string& fileId,
                                  int32_t index) override;
  void removeFile(const std::string& fileId) override;

 private:
  // Throws std::invalid_argument unless fileId looks like a UUID, so a
  // caller bug can never turn into a path outside root.
  std::filesystem::path fileDir(const std::string& fileId) const;

  std::filesystem::path root_;
};

}  // namespace file::storage
