#pragma once

#include <sodium.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace crypto {

// Files are encrypted with libsodium's secretstream: a random key per file,
// the plaintext cut into pieces of at most kFilePlainChunkBytes, each piece
// encrypted into one chunk that is uploaded on its own. The stream detects
// chunks that are modified, reordered, dropped, or a missing last chunk.
// See _Docs/API/File_API.md ("Encryption").

inline constexpr std::size_t kFilePlainChunkBytes = 1024 * 1024;
inline constexpr std::size_t kFileChunkOverhead =
    crypto_secretstream_xchacha20poly1305_ABYTES;  // 17 bytes

using FileKey =
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_KEYBYTES>;
using FileHeader =
    std::array<unsigned char, crypto_secretstream_xchacha20poly1305_HEADERBYTES>;
using Bytes = std::vector<unsigned char>;

// Number of chunks for a plaintext of this size (an empty file is one chunk).
std::int64_t fileChunkCount(std::int64_t plaintextSize);

// Total ciphertext size: plaintext + 17 bytes per chunk.
std::int64_t fileCiphertextSize(std::int64_t plaintextSize);

// Encrypts one file, chunk by chunk, in order. Generates its own key.
class FileEncryptor {
 public:
  FileEncryptor();
  ~FileEncryptor();
  FileEncryptor(const FileEncryptor&) = delete;
  FileEncryptor& operator=(const FileEncryptor&) = delete;

  const FileKey& key() const { return key_; }
  const FileHeader& header() const { return header_; }

  // Encrypts the next piece (at most kFilePlainChunkBytes). `last` marks the
  // end of the file; nothing can be encrypted after it.
  Bytes encryptChunk(const unsigned char* data, std::size_t size, bool last);

 private:
  crypto_secretstream_xchacha20poly1305_state state_{};
  FileKey key_{};
  FileHeader header_{};
  bool finished_ = false;
};

// Decrypts the chunks of one file, in order. Throws std::runtime_error on
// any chunk that does not authenticate.
class FileDecryptor {
 public:
  FileDecryptor(const FileKey& key, const FileHeader& header);
  ~FileDecryptor();
  FileDecryptor(const FileDecryptor&) = delete;
  FileDecryptor& operator=(const FileDecryptor&) = delete;

  Bytes decryptChunk(const unsigned char* data, std::size_t size);

  // True once the chunk marked as last by the sender was decrypted. A file
  // whose chunks all decrypt but never reach it was truncated.
  bool finished() const { return finished_; }

 private:
  crypto_secretstream_xchacha20poly1305_state state_{};
  bool finished_ = false;
};

}  // namespace crypto
