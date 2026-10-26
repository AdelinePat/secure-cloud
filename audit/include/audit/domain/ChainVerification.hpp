#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace audit::domain {

// Result of walking the whole hash chain.
struct ChainVerification {
  bool valid = true;
  int64_t checked = 0;                 // number of events verified
  std::optional<int64_t> first_broken_seq;
  std::string reason;                  // empty when valid
};

}  // namespace audit::domain
