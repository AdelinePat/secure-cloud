#include "file/storage/ChunkStorage.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace file::storage {

FilesystemChunkStorage::FilesystemChunkStorage(std::filesystem::path root)
    : root_(std::move(root)) {
  std::filesystem::create_directories(root_);
}

std::filesystem::path FilesystemChunkStorage::fileDir(
    const std::string& fileId) const {
  const bool safe =
      fileId.size() == 36 &&
      std::all_of(fileId.begin(), fileId.end(), [](unsigned char c) {
        return std::isxdigit(c) || c == '-';
      });
  if (!safe) {
    throw std::invalid_argument("Invalid file id for storage");
  }
  return root_ / fileId;
}

void FilesystemChunkStorage::write(const std::string& fileId, int32_t index,
                                   std::string_view bytes) {
  if (index < 0) throw std::invalid_argument("Negative chunk index");

  const auto dir = fileDir(fileId);
  std::filesystem::create_directories(dir);

  const auto target = dir / std::to_string(index);
  const auto temporary = dir / (std::to_string(index) + ".tmp");

  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
      throw std::runtime_error("Failed to write chunk " + target.string());
    }
  }

  std::filesystem::rename(temporary, target);
}

std::optional<std::string> FilesystemChunkStorage::read(
    const std::string& fileId, int32_t index) {
  if (index < 0) return std::nullopt;

  std::ifstream in(fileDir(fileId) / std::to_string(index), std::ios::binary);
  if (!in) return std::nullopt;

  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  if (in.bad()) {
    throw std::runtime_error("Failed to read chunk");
  }
  return bytes;
}

void FilesystemChunkStorage::removeFile(const std::string& fileId) {
  std::filesystem::remove_all(fileDir(fileId));
}

}  // namespace file::storage
