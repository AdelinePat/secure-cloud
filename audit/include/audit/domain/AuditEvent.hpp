#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace audit::domain {

// One immutable audit-log entry.
//
// The store owns `id`, `seq`, `recorded_at`, `prev_hash` and `hash`: callers
// (emitting microservices) provide everything else. `id` may be supplied by the
// caller as an idempotency key: re-sending the same event_id is a no-op and
// returns the event that was stored the first time.
struct AuditEvent {
  std::string id;                        // uuid v4, generated when empty
  int64_t seq = 0;                       // 1-based position in the chain
  std::string event_type;                // e.g. "file_uploaded", "login_failed"
  std::string service_name;              // emitting microservice, e.g. "file"
  std::optional<std::string> actor_id;   // user id, empty for system events
  std::optional<std::string> target_id;  // affected resource id
  std::string outcome;                   // "success" | "failure"
  std::string occurred_at;               // ISO8601 UTC; defaults to now
  std::string recorded_at;               // ISO8601 UTC, set by the store
  std::string metadata_json = "{}";      // canonical JSON object, <= 16 KiB
  std::string prev_hash;                 // hash of seq-1 (genesis for seq 1)
  std::string hash;                      // sha256 over this event
};

}  // namespace audit::domain
