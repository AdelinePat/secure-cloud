#include "audit/util/Time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace audit::util {

std::string formatIso8601(int64_t epoch_ms) {
  const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
  const int millis = static_cast<int>(epoch_ms % 1000);

  std::tm utc{};
  gmtime_r(&seconds, &utc);

  char buffer[32];
  const int written =
      std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                    utc.tm_min, utc.tm_sec, millis);
  return std::string(buffer, static_cast<std::size_t>(written));
}

std::string nowIso8601() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto millis =
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return formatIso8601(millis);
}

}  // namespace audit::util
