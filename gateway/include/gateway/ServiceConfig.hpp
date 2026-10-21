#pragma once

#include <cstdint>
#include <string>

namespace gateway {

struct ServiceEndpoint {
  std::string host;
  std::uint16_t port;
};

class ServiceConfig {
 public:
  static ServiceEndpoint auth();
};

}  // namespace gateway