#pragma once

#include <cstdint>
#include <string>

namespace audit::util {

// ISO8601 UTC with millisecond precision, e.g. "2026-10-21T09:30:12.345Z".
// Fixed width, so lexical order == chronological order.
std::string formatIso8601(int64_t epoch_ms);
std::string nowIso8601();

}  // namespace audit::util
