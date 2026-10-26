#include "gateway/ServiceConfig.hpp"

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace gateway {

ServiceEndpoint ServiceConfig::auth() {
  // Read the Auth service configuration
  const char* envHost = std::getenv("AUTH_HOST");
  const char* envPort = std::getenv("AUTH_PORT");

  if (envHost == nullptr || std::string(envHost).empty()) {
    throw std::runtime_error("AUTH_HOST is not defined");
  }

  if (envPort == nullptr) {
    throw std::runtime_error("AUTH_PORT is not defined");
  }

  const std::string portValue(envPort);
  std::size_t parsedCharacters = 0;

  const unsigned long parsedPort = std::stoul(portValue, &parsedCharacters);

  // Validate the complete port value and its range
  if (parsedCharacters != portValue.size() || parsedPort == 0 ||
      parsedPort > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Invalid AUTH_PORT value");
  }

  return {std::string(envHost), static_cast<std::uint16_t>(parsedPort)};
}

}  // namespace gateway