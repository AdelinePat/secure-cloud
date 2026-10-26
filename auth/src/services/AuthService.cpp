
#include "auth/services/AuthService.hpp"

#include "auth/crypto/PasswordHasher.hpp"

namespace auth::services {

AuthService::AuthService(repositories::IUserRepository& userRepository)
    : userRepository_(userRepository) {}

std::optional<LoginResult> AuthService::login(const LoginRequest& request) {
  // Find the user by username.
  const auto user = userRepository_.findByUsername(request.username);

  if (!user.has_value()) {
    return std::nullopt;
  }

  // Verify the supplied password against the stored Argon2id hash.
  if (!crypto::PasswordHasher::verify(request.password, user->password_hash)) {
    return std::nullopt;
  }

  // Token generation will be implemented in the next steps.
  return std::nullopt;
}

}  // namespace auth::services
