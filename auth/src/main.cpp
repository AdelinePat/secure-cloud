
#include <sodium.h>

#include <boost/asio.hpp>
#include <pqxx/pqxx>

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "auth/HttpServer.hpp"
#include "auth/crypto/JwtKeyManager.hpp"
#include "auth/repositories/PostgresSessionRepository.hpp"
#include "auth/repositories/PostgresUserRepository.hpp"
#include "auth/services/AuthService.hpp"

namespace {

// Read a required environment variable.
std::string requireEnv(const char* name) {
  const char* value = std::getenv(name);

  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(
        std::string("Missing environment variable: ") + name);
  }

  return value;
}

}  // namespace

int main() {
  try {
    // Initialize libsodium before using cryptographic operations.
    if (sodium_init() < 0) {
      throw std::runtime_error("Failed to initialize libsodium");
    }

    // Read the HTTP server configuration.
    const char* envPort = std::getenv("AUTH_PORT");

    const std::uint16_t port =
        envPort ? static_cast<std::uint16_t>(std::stoi(envPort)) : 8081;

    // Read database and JWT configuration.
    const std::string databaseUrl = requireEnv("AUTH_DATABASE_URL");

    const std::string privateKeyPath =
        requireEnv("AUTH_JWT_PRIVATE_KEY_PATH");

    const std::string publicKeyPath =
        requireEnv("AUTH_JWT_PUBLIC_KEY_PATH");

    // Open the PostgreSQL connection.
    pqxx::connection connection(databaseUrl);

    // Initialize the repositories.
    auth::repositories::PostgresUserRepository userRepository(connection);

    auth::repositories::PostgresSessionRepository sessionRepository(
        connection);

    // Load the JWT signing and verification keys.
    auth::crypto::JwtKeyManager jwtKeyManager(
        privateKeyPath,
        publicKeyPath);

    // Initialize the authentication business service.
    auth::services::AuthService authService(
        userRepository,
        sessionRepository,
        jwtKeyManager);

    // Initialize the HTTP server.
    boost::asio::io_context ioContext;

    auth::HttpServer server(
        ioContext,
        port,
        authService);

    std::cout << "Auth service listening on port "
              << port << std::endl;

    server.run();

  } catch (const std::exception& exception) {
    std::cerr << "Auth server error: "
              << exception.what() << std::endl;

    return 1;
  }

  return 0;
}
