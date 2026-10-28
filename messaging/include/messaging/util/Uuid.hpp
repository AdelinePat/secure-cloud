#pragma once

#include <string>

namespace messaging::util {

// True when `value` is a canonical 8-4-4-4-12 hexadecimal UUID.
bool isUuid(const std::string& value);

}  // namespace messaging::util
