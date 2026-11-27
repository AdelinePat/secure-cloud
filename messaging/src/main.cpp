#include <sodium.h>

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "common/audit/AuditClient.hpp"
#include "common/jwt/JwtVerifier.hpp"
#include "messaging/db/ConnectionPool.hpp"
#include "messaging/http/HttpServer.hpp"
#include "messaging/http/MessagingRouter.hpp"
#include "messaging/repositories/PostgresConversationRepository.hpp"
#include "messaging/repositories/PostgresMessageRepository.hpp"
#include "messaging/repositories/PostgresUserDirectory.hpp"

namespace {

std::string requireEnv(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') {
    throw std::runtime_error(std::string(name) + " is not configured");
  }
  return value;
}

std::string envOr(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return (value == nullptr || value[0] == '\0') ? fallback : value;
}

unsigned long parseNumber(const char* name, const std::string& value,
                          unsigned long max) {
  std::size_t parsed = 0;
  unsigned long number = 0;
  try {
    number = std::stoul(value, &parsed);
  } catch (const std::exception&) {
    parsed = 0;
  }
  if (parsed != value.size() || number == 0 || number > max) {
    throw std::runtime_error(std::string("Invalid ") + name + " value");
  }
  return number;
}

}  // namespace

int main() {
  try {
    if (sodium_init() < 0) {
      throw std::runtime_error("Failed to initialize libsodium");
    }

    const auto port = static_cast<std::uint16_t>(
        parseNumber("MESSAGING_PORT", envOr("MESSAGING_PORT", "8082"),
                    std::numeric_limits<std::uint16_t>::max()));

    // Requests handled at the same time. Each one holds a database
    // connection, so this is also the size of the connection pool.
    const std::size_t workers =
        parseNumber("MESSAGING_WORKERS", envOr("MESSAGING_WORKERS", "8"), 64);

    // Load the Auth public key once at startup. verify() is thread-safe.
    common::jwt::JwtVerifier jwtVerifier(
        requireEnv("MESSAGING_JWT_PUBLIC_KEY_PATH"));

    messaging::db::ConnectionPool pool(
        "host=" + requireEnv("DB_HOST") + " port=" + requireEnv("DB_PORT") +
            " user=" + requireEnv("POSTGRES_USER") +
            " password=" + requireEnv("POSTGRES_PASSWORD") +
            " dbname=" + requireEnv("POSTGRES_DB"),
        workers);
    std::cout << "Messaging connected to PostgreSQL (" << pool.size()
              << " connections)" << std::endl;

    // Best-effort emitter for security-relevant events.
    // Disabled unless AUDIT_ENABLED is true;
    // a failure never affects message handling.
    // emit() is thread-safe.
    const auto auditConfig = common::audit::Config::fromEnv("messaging");
    common::audit::Client auditClient(auditConfig);
    if (auditClient.enabled()) {
      std::cout << "Audit emitter enabled -> " << auditConfig.url << " as "
                << auditClient.serviceName() << std::endl;
    }

    const messaging::http::TokenVerifier verifyToken =
        [&jwtVerifier](const std::string& token) {
          return jwtVerifier.verify(token);
        };
    const messaging::http::AuditEmitter audit =
        [&auditClient](const common::audit::Event& event) {
          auditClient.emit(event);
        };

    // Runs on a worker thread: borrow a connection, build the repositories
    // and a router around it, and handle the request.
    auto handler = [&](const messaging::http::Request& request) {
      auto lease = pool.acquire();
      messaging::repositories::PostgresConversationRepository conversations(
          lease.connection());
      messaging::repositories::PostgresMessageRepository messages(
          lease.connection());
      messaging::repositories::PostgresUserDirectory users(lease.connection());

      messaging::http::MessagingRouter router({conversations, messages, users},
                                              verifyToken, audit);
      return router.route(request);
    };

    boost::asio::io_context ioContext;
    messaging::http::HttpServer server(ioContext, port, handler, workers);
    server.start();

    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);
    signals.async_wait([&ioContext](const boost::system::error_code& ec, int) {
      if (!ec) ioContext.stop();
    });

    std::cout << "Messaging HTTP server listening on port "
              << server.localPort() << " (" << workers << " workers)"
              << std::endl;

    ioContext.run();
  } catch (const std::exception& e) {
    std::cerr << "Messaging server error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
