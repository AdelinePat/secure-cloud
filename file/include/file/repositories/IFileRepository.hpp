#pragma once

#include <optional>
#include <string>
#include <vector>

#include "file/domain/FileChunk.hpp"
#include "file/domain/FileRecord.hpp"

namespace file::repositories {

class IFileRepository {
 public:
  virtual ~IFileRepository() = default;

  // Persists metadata only. Chunk bytes are written to the storage directory
  // by the caller; this repository only records that they arrived.
  virtual domain::FileRecord create(const domain::FileRecord& file) = 0;

  // Hides soft-deleted files.
  virtual std::optional<domain::FileRecord> findById(const std::string& id) = 0;

  // Returns false if no row matched. Callers pass one of
  // UPLOADING/AVAILABLE/FAILED/DELETED; AVAILABLE also sets completed_at.
  virtual bool updateStatus(const std::string& id, const std::string& status) = 0;

  // Sets deleted_at and status='DELETED' in one step.
  virtual bool softDelete(const std::string& id) = 0;

  // Records a received chunk. Re-uploading the same index replaces the
  // previous record, so interrupted uploads can simply resend it.
  virtual void upsertChunk(const domain::FileChunk& chunk) = 0;

  // Chunks received so far, ordered by index.
  virtual std::vector<domain::FileChunk> listChunks(const std::string& fileId) = 0;

  // The caller's role in a conversation, or nullopt if not a participant.
  // Reads messaging's conversation_participants table (shared database).
  virtual std::optional<std::string> findParticipantRole(
      const std::string& conversationId, const std::string& userId) = 0;
};

}  // namespace file::repositories
