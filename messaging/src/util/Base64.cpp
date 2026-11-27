#include "messaging/util/Base64.hpp"

#include <sodium.h>

#include <cstring>

namespace messaging::util {

std::optional<std::vector<unsigned char>> decodeBase64(
    const std::string& encoded, std::size_t maxDecodedSize) {
  if (encoded.empty() || encoded.size() > maxDecodedSize * 2 + 8) {
    return std::nullopt;
  }

  std::vector<unsigned char> decoded(maxDecodedSize);
  std::size_t decodedLength = 0;
  const char* end = nullptr;

  const int result = sodium_base642bin(
      decoded.data(), decoded.size(), encoded.data(), encoded.size(), nullptr,
      &decodedLength, &end, sodium_base64_VARIANT_ORIGINAL);

  if (result != 0 || end != encoded.data() + encoded.size()) {
    return std::nullopt;
  }

  decoded.resize(decodedLength);
  return decoded;
}

std::string encodeBase64(const std::vector<unsigned char>& data) {
  std::string encoded(
      sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL),
      '\0');

  sodium_bin2base64(encoded.data(), encoded.size(), data.data(), data.size(),
                    sodium_base64_VARIANT_ORIGINAL);

  encoded.resize(std::strlen(encoded.c_str()));
  return encoded;
}

}  // namespace messaging::util
