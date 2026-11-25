#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "file/domain/FileRecord.hpp"
#include "file/http/HttpServer.hpp"
#include "file/repositories/IFileRepository.hpp"
#include "file/storage/ChunkStorage.hpp"

namespace file::http {

// Largest ciphertext chunk: 1 MiB of plaintext plus libsodium secretstream's
// 17-byte per-message overhead (crypto_secretstream_xchacha20poly1305_ABYTES).
inline constexpr std::size_t kMaxChunkBytes = 1024 * 1024 + 17;

// Largest file accepted, in ciphertext bytes.
inline constexpr std::int64_t kMaxFileBytes = 2LL * 1024 * 1024 * 1024;

// Returns the authenticated user id for a bearer token, or nullopt.
// In production this is common::jwt::JwtVerifier::verify; tests stub it.
using TokenVerifier =
    std::function<std::optional<std::string>(const std::string& token)>;

// Maps HTTP requests to file operations. Paths are the file service's
// internal ones (the gateway strips its public prefix).
//
//   POST   /files                      create an upload (JSON body)
//   GET    /files/{id}                 metadata + chunks received so far
//   PUT    /files/{id}/chunks/{n}      upload ciphertext chunk n (raw body)
//   POST   /files/{id}/complete        mark AVAILABLE once every chunk is in
//   GET    /files/{id}/chunks/{n}      download ciphertext chunk n
//   DELETE /files/{id}                 uploader or conversation admin
//
// Access is membership of the file's conversation. Only the uploader may
// send chunks or complete the upload.
class FileRouter {
 public:
  FileRouter(repositories::IFileRepository& repository,
             storage::IChunkStorage& storage, TokenVerifier verifyToken);

  Response route(const Request& request);

 private:
  // User id from "Authorization: Bearer <jwt>", or nullopt.
  std::optional<std::string> authenticate(const Request& request) const;

  Response createFile(const Request& request, const std::string& userId);
  Response getFile(const Request& request, const std::string& userId,
                   const std::string& fileId);
  Response putChunk(const Request& request, const std::string& userId,
                    const std::string& fileId, int32_t index);
  Response completeFile(const Request& request, const std::string& userId,
                        const std::string& fileId);
  Response getChunk(const Request& request, const std::string& userId,
                    const std::string& fileId, int32_t index);
  Response deleteFile(const Request& request, const std::string& userId,
                      const std::string& fileId);

  repositories::IFileRepository& repository_;
  storage::IChunkStorage& storage_;
  TokenVerifier verifyToken_;
};

}  // namespace file::http
