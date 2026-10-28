#pragma once

#include <string>
#include <vector>

namespace messaging::util {

// Base64 (standard alphabet, with padding) helpers. The client sends and
// receives ciphertext/nonce as base64; the server never interprets them.
std::string base64Encode(const std::vector<unsigned char>& bytes);
std::vector<unsigned char> base64Decode(const std::string& encoded);

}  // namespace messaging::util
