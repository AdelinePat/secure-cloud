#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace messaging::util {

// Standard (padded) base64. Returns nullopt for malformed input or when the
// decoded data would exceed maxDecodedSize. Requires sodium_init().
std::optional<std::vector<unsigned char>> decodeBase64(
    const std::string& encoded, std::size_t maxDecodedSize);

std::string encodeBase64(const std::vector<unsigned char>& data);

}  // namespace messaging::util
