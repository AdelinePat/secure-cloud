#pragma once

#include <optional>
#include <string>

#include "auth/repositories/IUserRepository.hpp"

namespace auth::services {

struct LoginRequest {
  std::string username;
  std::string password;
};

struct LoginResult {
  std::string accessToken;
  std::string refreshToken;
};

class AuthService {
 public:
  explicit AuthService(repositories::IUserRepository& userRepository);
  std::optional<LoginResult> login(const LoginRequest& request);

 private:
  repositories::IUserRepository& userRepository_;
};

}  // namespace auth::services
