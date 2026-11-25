#include "crypto/FileCrypto.hpp"

#include <stdexcept>

namespace crypto {

std::int64_t fileChunkCount(std::int64_t plaintextSize) {
  if (plaintextSize < 0) throw std::invalid_argument("Negative file size");
  const auto chunk = static_cast<std::int64_t>(kFilePlainChunkBytes);
  return plaintextSize == 0 ? 1 : (plaintextSize + chunk - 1) / chunk;
}

std::int64_t fileCiphertextSize(std::int64_t plaintextSize) {
  return plaintextSize + static_cast<std::int64_t>(kFileChunkOverhead) *
                             fileChunkCount(plaintextSize);
}

FileEncryptor::FileEncryptor() {
  crypto_secretstream_xchacha20poly1305_keygen(key_.data());
  if (crypto_secretstream_xchacha20poly1305_init_push(&state_, header_.data(),
                                                      key_.data()) != 0) {
    throw std::runtime_error("Failed to initialise file encryption");
  }
}

FileEncryptor::~FileEncryptor() {
  sodium_memzero(&state_, sizeof(state_));
  sodium_memzero(key_.data(), key_.size());
}

Bytes FileEncryptor::encryptChunk(const unsigned char* data, std::size_t size,
                                  bool last) {
  if (finished_) throw std::logic_error("File encryption already finished");
  if (size > kFilePlainChunkBytes) throw std::invalid_argument("Chunk too large");

  Bytes chunk(size + kFileChunkOverhead);
  const unsigned char tag = last ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
                                 : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
  if (crypto_secretstream_xchacha20poly1305_push(&state_, chunk.data(), nullptr,
                                                 data, size, nullptr, 0,
                                                 tag) != 0) {
    throw std::runtime_error("Failed to encrypt file chunk");
  }
  finished_ = last;
  return chunk;
}

FileDecryptor::FileDecryptor(const FileKey& key, const FileHeader& header) {
  if (crypto_secretstream_xchacha20poly1305_init_pull(&state_, header.data(),
                                                      key.data()) != 0) {
    throw std::runtime_error("Invalid file encryption header");
  }
}

FileDecryptor::~FileDecryptor() { sodium_memzero(&state_, sizeof(state_)); }

Bytes FileDecryptor::decryptChunk(const unsigned char* data, std::size_t size) {
  if (finished_) throw std::runtime_error("Data after the last file chunk");
  if (size < kFileChunkOverhead ||
      size > kFilePlainChunkBytes + kFileChunkOverhead) {
    throw std::runtime_error("Invalid file chunk size");
  }

  Bytes plain(size - kFileChunkOverhead);
  unsigned char tag = 0;
  if (crypto_secretstream_xchacha20poly1305_pull(&state_, plain.data(), nullptr,
                                                 &tag, data, size, nullptr,
                                                 0) != 0) {
    throw std::runtime_error("File chunk failed authentication");
  }
  finished_ = tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL;
  return plain;
}

}  // namespace crypto
