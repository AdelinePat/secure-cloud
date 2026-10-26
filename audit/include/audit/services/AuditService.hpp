#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audit/domain/AuditEvent.hpp"
#include "audit/domain/AuditQuery.hpp"
#include "audit/domain/ChainVerification.hpp"
#include "audit/repositories/IAuditRepository.hpp"
#include "audit/services/ServicePolicy.hpp"

namespace audit::services {

struct AppendResult {
  domain::AuditEvent event;
  bool created = true;  // false when an idempotent retry returned an existing row
};

// Business rules for the audit log:
//  - validate and normalise incoming events (metadata canonicalisation, fields)
//  - enforce MS-to-MS identity
//  - seal events into the tamper-evident hash chain
//  - expose read + verification operations
class AuditService {
 public:
  AuditService(repositories::IAuditRepository& repository, ServicePolicy policy);

  // Appends `draft` on behalf of `caller` (client-certificate CN, or empty in
  // dev mode). Returns the stored event, including id/seq/hashes. Re-sending a
  // caller-supplied `draft.id` returns the previously stored event unchanged
  // (idempotent retry) with `created == false`.
  AppendResult append(domain::AuditEvent draft, std::string_view caller);

  std::optional<domain::AuditEvent> get(const std::string& id,
                                        std::string_view caller) const;

  std::vector<domain::AuditEvent> query(domain::AuditQuery query,
                                        std::string_view caller) const;

  // Recomputes every hash and link; detects edits, deletions and reordering
  // within the stored chain. Truncating the tail is only detectable with a
  // signed checkpoint (see the API doc's open items).
  domain::ChainVerification verifyChain(std::string_view caller) const;

  int64_t count() const;
  bool healthy() const;

 private:
  void normalize(domain::AuditEvent& event) const;

  repositories::IAuditRepository& repository_;
  ServicePolicy policy_;
};

}  // namespace audit::services
