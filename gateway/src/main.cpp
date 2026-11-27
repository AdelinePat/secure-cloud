#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "gateway/HttpServer.hpp"

int main() {
  try {
    // Read the listening port from the environment
    std::uint16_t port = 8080;

    if (const char* envPort = std::getenv("GATEWAY_PORT")) {
      const std::string value(envPort);

      std::size_t parsedCharacters = 0;
      const unsigned long parsedPort = std::stoul(value, &parsedCharacters);

      // Reject invalid or out-of-range port values
      if (parsedCharacters != value.size() || parsedPort == 0 ||
          parsedPort > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("Invalid GATEWAY_PORT value");
      }

      port = static_cast<std::uint16_t>(parsedPort);
    }

    // HTTPS when both PEM files are configured, plain HTTP otherwise.
    std::optional<gateway::TlsFiles> tls;
    const char* certPath = std::getenv("GATEWAY_TLS_CERT");
    const char* keyPath = std::getenv("GATEWAY_TLS_KEY");
    const bool hasCert = certPath != nullptr && certPath[0] != '\0';
    const bool hasKey = keyPath != nullptr && keyPath[0] != '\0';
    if (hasCert != hasKey) {
      throw std::runtime_error(
          "Set both GATEWAY_TLS_CERT and GATEWAY_TLS_KEY, or neither");
    }
    if (hasCert) {
      tls = gateway::TlsFiles{certPath, keyPath};
    }

    boost::asio::io_context ioContext;

    gateway::HttpServer server(ioContext, port, tls);

    server.start();

    // Register operating system signals for graceful shutdown
    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);

    signals.async_wait(
        [&ioContext](const boost::system::error_code& ec, int signal) {
          if (!ec) {
            std::cout << "\nShutdown signal received: " << signal << '\n';

            ioContext.stop();
          }
        });

    std::cout << "SecureChat Gateway listening on port " << server.localPort()
              << (server.tlsEnabled() ? " (HTTPS)" : " (plain HTTP, no TLS)")
              << std::endl;

    // Process asynchronous network operations
    ioContext.run();

  } catch (const std::exception& exception) {
    std::cerr << "Gateway startup failed: " << exception.what() << '\n';

    return 1;
  }

  return 0;
}