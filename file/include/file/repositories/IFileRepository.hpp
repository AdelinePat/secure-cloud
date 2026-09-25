#pragma once

#include <optional>
#include <string>

#include "file/domain/FileRecord.hpp"

namespace file::repositories {

class IFileRepository {
 public:
  virtual ~IFileRepository() = default;

  // Persists metadata only — the encrypted bytes themselves go to object
  // storage (MinIO/Garage) via storage_ref, not through this repository.
  virtual domain::FileRecord create(const domain::FileRecord& file) = 0;

  virtual std::optional<domain::FileRecord> findById(const std::string& id) = 0;

  // Returns false if no row matched. Enforced against the DB CHECK constraint
  // on `status` — callers pass one of UPLOADING/AVAILABLE/FAILED/DELETED.
  virtual bool updateStatus(const std::string& id, const std::string& status) = 0;

  // Sets deleted_at and status='DELETED' in one step.
  virtual bool softDelete(const std::string& id) = 0;
};

}  // namespace file::repositories
