#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace audit::domain {

// Filters accepted by GET /audit/events.
//
// `from`/`to` are inclusive ISO8601 UTC bounds compared against `occurred_at`
// as strings (ISO8601 UTC sorts lexicographically, which is why the store keeps
// timestamps as strings). `before_seq` / `after_seq` implement keyset
// pagination so a large chain can be walked without OFFSET costs.
struct AuditQuery {
  std::optional<std::string> event_type;
  std::optional<std::string> service_name;
  std::optional<std::string> actor_id;
  std::optional<std::string> target_id;
  std::optional<std::string> outcome;
  std::optional<std::string> from;
  std::optional<std::string> to;
  std::optional<int64_t> before_seq;
  std::optional<int64_t> after_seq;
  int limit = 50;
  bool ascending = false;
};

}  // namespace audit::domain
