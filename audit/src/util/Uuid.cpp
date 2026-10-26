#include "audit/util/Uuid.hpp"

#include <openssl/rand.h>

#include <cstdio>
#include <stdexcept>

namespace audit::util {

std::string uuidV4() {
  unsigned char bytes[16];
  if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
    throw std::runtime_error("RAND_bytes failed while generating a uuid");
  }

  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0F) | 0x40);  // version 4
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3F) | 0x80);  // variant

  char buffer[37];
  std::snprintf(buffer, sizeof(buffer),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                "%02x%02x%02x%02x%02x%02x",
                bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
                bytes[12], bytes[13], bytes[14], bytes[15]);
  return std::string(buffer, 36);
}

}  // namespace audit::util
