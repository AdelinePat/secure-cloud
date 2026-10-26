#pragma once

#include <memory>
#include <string>

#include "audit/repositories/IAuditRepository.hpp"

namespace audit::repositories {

struct MongoConfig {
  // When non-empty this wins over the individual fields below, e.g.
  // "mongodb://user:pass@localhost:27017/auditDB?authSource=auditDB".
  std::string uri;
  std::string host = "localhost";
  int port = 27017;
  std::string database = "auditDB";
  std::string username;
  std::string password;
  std::string auth_source;  // defaults to `database`
  std::string collection = "auditLogs";
};

// libmongoc-backed append-only repository.
//
// Concurrency: append serialisation is enforced by unique indexes on `seq`,
// `prev_hash` and `hash` rather than a multi-document transaction, so it works
// against a standalone mongod (no replica set required). Two writers racing for
// the same head produce one winner and one DuplicateKeyError; the service then
// retries against the new head.
class MongoAuditRepository final : public IAuditRepository {
 public:
  explicit MongoAuditRepository(MongoConfig config);
  ~MongoAuditRepository() override;

  MongoAuditRepository(const MongoAuditRepository&) = delete;
  MongoAuditRepository& operator=(const MongoAuditRepository&) = delete;

  // Idempotent; creates the unique/indexes the append protocol relies on.
  // Throws audit::AuditError on failure.
  void ensureIndexes();

  void insert(const domain::AuditEvent& event) override;
  std::optional<domain::AuditEvent> findById(const std::string& id) override;
  std::optional<domain::AuditEvent> latest() override;
  std::vector<domain::AuditEvent> find(const domain::AuditQuery& query) override;
  void forEachOrdered(
      const std::function<bool(const domain::AuditEvent&)>& visitor) override;
  int64_t count() override;
  bool healthy() override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace audit::repositories
