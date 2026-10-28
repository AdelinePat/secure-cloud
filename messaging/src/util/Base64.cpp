#include "messaging/util/Base64.hpp"

#include <sodium.h>

#include <stdexcept>

namespace messaging::util {

std::string base64Encode(const std::vector<unsigned char>& bytes) {
  if (bytes.empty()) return {};

  const std::size_t encodedLength = sodium_base64_encoded_len(
      bytes.size(), sodium_base64_VARIANT_ORIGINAL);

  std::string encoded(encodedLength, '\0');
  sodium_bin2base64(encoded.data(), encoded.size(), bytes.data(), bytes.size(),
                    sodium_base64_VARIANT_ORIGINAL);

  // libsodium writes a terminating NUL inside the buffer.
  encoded.resize(encodedLength - 1);
  return encoded;
}

std::vector<unsigned char> base64Decode(const std::string& encoded) {
  if (encoded.empty()) return {};

  // 4 encoded characters decode to at most 3 bytes.
  std::vector<unsigned char> decoded((encoded.size() / 4) * 3 + 3);

  std::size_t decodedLength = 0;
  const int result = sodium_base642bin(
      decoded.data(), decoded.size(), encoded.data(), encoded.size(), nullptr,
      &decodedLength, nullptr, sodium_base64_VARIANT_ORIGINAL);

  if (result != 0) {
    throw std::invalid_argument("Invalid base64 value");
  }

  decoded.resize(decodedLength);
  return decoded;
}

}  // namespace messaging::util
