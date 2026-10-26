#pragma once

#include <optional>
#include <string>

#include "auth/crypto/JwtKeyManager.hpp"
#include "auth/repositories/ISessionRepository.hpp"
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

struct RefreshRequest {
  std::string refreshToken;
};

struct RefreshResult {
  std::string accessToken;
  std::string refreshToken;
};

class AuthService {
 public:
  AuthService(repositories::IUserRepository& userRepository,
              repositories::ISessionRepository& sessionRepository,
              const crypto::JwtKeyManager& jwtKeyManager);

  std::optional<LoginResult> login(const LoginRequest& request);

  std::optional<RefreshResult> refresh(const RefreshRequest& request);

 private:
  repositories::IUserRepository& userRepository_;
  repositories::ISessionRepository& sessionRepository_;
  const crypto::JwtKeyManager& jwtKeyManager_;
};

}  // namespace auth::services
