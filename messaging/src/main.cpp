#include <sodium.h>

#include <boost/asio.hpp>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

#include "messaging/crypto/JwtVerifier.hpp"
#include "messaging/http/HttpRouter.hpp"
#include "messaging/http/HttpServer.hpp"
#include "messaging/repositories/PostgresConversationRepository.hpp"
#include "messaging/repositories/PostgresMessageRepository.hpp"
#include "messaging/repositories/PostgresUserDirectory.hpp"
#include "messaging/services/MessagingService.hpp"

namespace {

std::string requireEnv(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    throw std::runtime_error(std::string("Missing environment variable: ") +
                             name);
  }
  return value;
}

std::string envOr(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return fallback;
  return value;
}

std::string buildDatabaseUrl() {
  const char* explicitUrl = std::getenv("MESSAGING_DATABASE_URL");
  if (explicitUrl != nullptr && *explicitUrl != '\0') return explicitUrl;

  return "host=" + envOr("DB_HOST", "app-db") +
         " port=" + envOr("DB_PORT", "5432") +
         " dbname=" + requireEnv("POSTGRES_DB") +
         " user=" + requireEnv("POSTGRES_USER") +
         " password=" + requireEnv("POSTGRES_PASSWORD");
}

std::uint16_t readPort() {
  const std::string value = envOr("MESSAGING_PORT", "8082");

  std::size_t parsedCharacters = 0;
  const unsigned long parsedPort = std::stoul(value, &parsedCharacters);

  if (parsedCharacters != value.size() || parsedPort == 0 ||
      parsedPort > 65535) {
    throw std::runtime_error("Invalid MESSAGING_PORT value");
  }

  return static_cast<std::uint16_t>(parsedPort);
}

}  // namespace

int main() {
  try {
    if (sodium_init() < 0) {
      throw std::runtime_error("Failed to initialize libsodium");
    }

    const std::uint16_t port = readPort();

    const std::string databaseUrl = buildDatabaseUrl();

    const std::string publicKeyPath =
        std::getenv("MESSAGING_JWT_PUBLIC_KEY_PATH") != nullptr
            ? requireEnv("MESSAGING_JWT_PUBLIC_KEY_PATH")
            : requireEnv("AUTH_JWT_PUBLIC_KEY_PATH");

    pqxx::connection connection(databaseUrl);

    messaging::repositories::PostgresConversationRepository
        conversationRepository(connection);
    messaging::repositories::PostgresMessageRepository messageRepository(
        connection);
    messaging::repositories::PostgresUserDirectory userDirectory(connection);

    messaging::services::MessagingService messagingService(
        conversationRepository, messageRepository, userDirectory);

    messaging::crypto::JwtVerifier jwtVerifier(publicKeyPath);

    messaging::http::HttpRouter router(messagingService, jwtVerifier);

    boost::asio::io_context ioContext;
    messaging::http::HttpServer server(ioContext, port, router);

    std::cout << "Messaging service listening on port " << port << std::endl;

    server.run();

  } catch (const std::exception& exception) {
    std::cerr << "Messaging server error: " << exception.what() << std::endl;
    return 1;
  }

  return 0;
}
