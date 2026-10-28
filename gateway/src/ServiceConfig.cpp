#include "gateway/ServiceConfig.hpp"

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace gateway {

namespace {

ServiceEndpoint readEndpoint(const char* hostVariable,
                             const char* portVariable) {
  const char* envHost = std::getenv(hostVariable);
  const char* envPort = std::getenv(portVariable);

  if (envHost == nullptr || std::string(envHost).empty()) {
    throw std::runtime_error(std::string(hostVariable) + " is not defined");
  }

  if (envPort == nullptr) {
    throw std::runtime_error(std::string(portVariable) + " is not defined");
  }

  const std::string portValue(envPort);
  std::size_t parsedCharacters = 0;

  unsigned long parsedPort = 0;
  try {
    parsedPort = std::stoul(portValue, &parsedCharacters);
  } catch (const std::exception&) {
    throw std::runtime_error(std::string("Invalid ") + portVariable + " value");
  }

  // Validate the complete port value and its range.
  if (parsedCharacters != portValue.size() || parsedPort == 0 ||
      parsedPort > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error(std::string("Invalid ") + portVariable + " value");
  }

  return {std::string(envHost), static_cast<std::uint16_t>(parsedPort)};
}

}  // namespace

ServiceEndpoint ServiceConfig::auth() {
  return readEndpoint("AUTH_HOST", "AUTH_PORT");
}

ServiceEndpoint ServiceConfig::messaging() {
  return readEndpoint("MESSAGING_HOST", "MESSAGING_PORT");
}

}  // namespace gateway
