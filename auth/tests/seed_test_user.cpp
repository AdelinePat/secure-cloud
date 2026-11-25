
#include <sodium.h>
#include <pqxx/pqxx>

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "auth/crypto/PasswordHasher.hpp"
#include "auth/domain/User.hpp"
#include "auth/repositories/PostgresUserRepository.hpp"

int main(int argc, char** argv) {
  try {
    if (sodium_init() < 0) {
      throw std::runtime_error("Failed to initialize libsodium");
    }

    const char* databaseUrl = std::getenv("AUTH_DATABASE_URL");
    const char* testPassword = std::getenv("AUTH_TEST_PASSWORD");

    if (databaseUrl == nullptr || *databaseUrl == '\0' ||
        testPassword == nullptr || *testPassword == '\0') {
      throw std::runtime_error("Missing required environment variable");
    }

    pqxx::connection connection(databaseUrl);

    auth::repositories::PostgresUserRepository userRepository(connection);

    auth::domain::User user{};

    user.username = argc > 1 ? argv[1] : "alice_test";
    if (user.username != "alice_test" && user.username != "bob_test")
      throw std::runtime_error("Expected alice_test or bob_test");
    user.email = user.username == "alice_test" ? "alice.test@example.com" : "bob.test@example.com";

    // Never store plaintext passwords.
    user.password_hash =
        auth::crypto::PasswordHasher::hash(testPassword);

    // Placeholder values for this authentication integration test only.
    user.public_key = "";
    user.key_algorithm = "";

    const auto createdUser = userRepository.create(user);

    std::cout << "Test user created successfully.\n";
    std::cout << "User ID: " << createdUser.id << '\n';
    std::cout << "Username: " << createdUser.username << '\n';

  } catch (const std::exception& exception) {
    std::cerr << "Test user creation failed: "
              << exception.what() << '\n';

    return 1;
  }

  return 0;
}
