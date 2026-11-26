#pragma once

#include <cstddef>
#include <string_view>

namespace common::security {

inline constexpr std::size_t maxAccessTokenSize = 4096;
inline constexpr char accessTokenAudience[] = "securechat-api";

inline bool isUuid(std::string_view value) {
  if (value.size() != 36) {
    return false;
  }

  for (std::size_t i = 0; i < value.size(); ++i) {
    const char c = value[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') {
        return false;
      }
    } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                 (c >= 'A' && c <= 'F'))) {
      return false;
    }
  }
  return true;
}

}  // namespace common::security
