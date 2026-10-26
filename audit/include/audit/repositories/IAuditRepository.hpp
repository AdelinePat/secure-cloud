#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "audit/domain/AuditEvent.hpp"
#include "audit/domain/AuditQuery.hpp"

namespace audit::repositories {

// Raised when the store rejects an insert because `id`, `seq` or `prev_hash`
// already exists. AuditService treats it as an optimistic-concurrency signal:
// re-read the head, rebuild seq/prev_hash/hash, retry. It also backs the
// idempotency guarantee for caller-supplied event ids.
class DuplicateKeyError : public std::runtime_error {
 public:
  DuplicateKeyError() : std::runtime_error("duplicate key") {}
};

// Append-only storage. There is deliberately no update or delete operation:
// audit rows are facts, and tampering is detected by the hash chain.
class IAuditRepository {
 public:
  virtual ~IAuditRepository() = default;

  // Stores a fully-formed event. Throws DuplicateKeyError on id/seq/prev_hash
  // collisions and audit::AuditError on storage failures.
  virtual void insert(const domain::AuditEvent& event) = 0;

  virtual std::optional<domain::AuditEvent> findById(const std::string& id) = 0;

  // Event with the highest seq, or nullopt when the chain is empty.
  virtual std::optional<domain::AuditEvent> latest() = 0;

  virtual std::vector<domain::AuditEvent> find(const domain::AuditQuery& query) = 0;

  // Walks the chain in seq order. The visitor returns false to stop early.
  virtual void forEachOrdered(
      const std::function<bool(const domain::AuditEvent&)>& visitor) = 0;

  virtual int64_t count() = 0;
  virtual bool healthy() = 0;
};

}  // namespace audit::repositories
