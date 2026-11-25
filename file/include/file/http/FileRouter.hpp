#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "common/audit/AuditClient.hpp"
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

// Receives security-relevant events (common::audit::Client::emit in
// production). Must not throw; an empty function disables auditing.
using AuditEmitter = std::function<void(const common::audit::Event& event)>;

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
//
// Audit events (target_id = file id unless noted):
//   file_upload_started   upload created
//   file_uploaded         upload completed (first time only)
//   file_download_started chunk 0 fetched (one event per download, not per chunk)
//   file_deleted          file deleted
//   file_access_denied    any 403 (outcome "failure"; target_id is the
//                         conversation id when creating)
class FileRouter {
 public:
  FileRouter(repositories::IFileRepository& repository,
             storage::IChunkStorage& storage, TokenVerifier verifyToken,
             AuditEmitter audit = {});

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

  // The file and the caller's role in its conversation, or the 404/403 to
  // send instead (403s are audited with the given action).
  struct Access {
    std::optional<domain::FileRecord> file;
    std::string role;
    std::optional<Response> error;
  };
  Access loadAccessible(const Request& request, const std::string& fileId,
                        const std::string& userId, const char* action);

  // Emits file_access_denied and returns 403 FORBIDDEN.
  Response deny(const Request& request, const std::string& userId,
                const char* action, const std::string& targetId,
                const std::string& conversationId, const char* reason);

  void emit(const char* eventType, const std::string& userId,
            const std::string& targetId, nlohmann::json metadata,
            const char* outcome = "success");

  repositories::IFileRepository& repository_;
  storage::IChunkStorage& storage_;
  TokenVerifier verifyToken_;
  AuditEmitter audit_;
};

}  // namespace file::http
