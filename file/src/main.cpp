#include <sodium.h>

#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>

#include "common/jwt/JwtVerifier.hpp"
#include "file/http/FileRouter.hpp"
#include "file/http/HttpServer.hpp"
#include "file/repositories/PostgresFileRepository.hpp"

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

std::uint16_t parsePort(const std::string& value) {
  std::size_t parsed = 0;
  const unsigned long port = std::stoul(value, &parsed);
  if (parsed != value.size() || port == 0 ||
      port > std::numeric_limits<std::uint16_t>::max()) {
    throw std::runtime_error("Invalid FILE_PORT value");
  }
  return static_cast<std::uint16_t>(port);
}

}  // namespace

int main() {
  try {
    if (sodium_init() < 0) {
      throw std::runtime_error("Failed to initialize libsodium");
    }

    const std::uint16_t port = parsePort(envOr("FILE_PORT", "8083"));

    // Ciphertext chunks are stored here as <file_id>/<chunk_index>.
    const std::filesystem::path storageDir =
        envOr("FILE_STORAGE_DIR", "/app/file-storage");
    std::filesystem::create_directories(storageDir);

    common::jwt::JwtVerifier jwtVerifier(
        requireEnv("FILE_JWT_PUBLIC_KEY_PATH"));

    pqxx::connection dbConnection(
        "host=" + requireEnv("DB_HOST") + " port=" + requireEnv("DB_PORT") +
        " user=" + requireEnv("POSTGRES_USER") +
        " password=" + requireEnv("POSTGRES_PASSWORD") +
        " dbname=" + requireEnv("POSTGRES_DB"));
    file::repositories::PostgresFileRepository repository(dbConnection);
    std::cout << "File service connected to PostgreSQL" << std::endl;

    file::http::FileRouter router(
        repository,
        [&jwtVerifier](const std::string& token) {
          return jwtVerifier.verify(token);
        });

    boost::asio::io_context ioContext;
    file::http::HttpServer server(
        ioContext, port,
        [&router](const file::http::Request& request) {
          return router.route(request);
        });
    server.start();

    boost::asio::signal_set signals(ioContext, SIGINT, SIGTERM);
    signals.async_wait([&ioContext](const boost::system::error_code& ec, int) {
      if (!ec) ioContext.stop();
    });

    std::cout << "File HTTP server listening on port " << server.localPort()
              << " (storage: " << storageDir.string() << ")" << std::endl;

    ioContext.run();
  } catch (const std::exception& e) {
    std::cerr << "File service error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
