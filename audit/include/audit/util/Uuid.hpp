#pragma once

#include <string>

namespace audit::util {

// Random RFC 4122 version 4 UUID (36 chars, lowercase).
std::string uuidV4();

}  // namespace audit::util
