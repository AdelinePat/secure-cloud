#include "messaging/util/Uuid.hpp"

#include <cctype>

namespace messaging::util {

bool isUuid(const std::string& value) {
  if (value.size() != 36) return false;

  for (std::size_t i = 0; i < value.size(); ++i) {
    const bool isHyphenPosition = (i == 8 || i == 13 || i == 18 || i == 23);

    if (isHyphenPosition) {
      if (value[i] != '-') return false;
      continue;
    }

    if (!std::isxdigit(static_cast<unsigned char>(value[i]))) return false;
  }

  return true;
}

}  // namespace messaging::util
