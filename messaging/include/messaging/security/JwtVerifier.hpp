#pragma once

#include <openssl/evp.h>

#include <memory>
#include <optional>
#include <string>

namespace messaging::security {

class JwtVerifier {
 public:
  explicit JwtVerifier(const std::string& publicKeyPath);

  // Validate the JWT and return its authenticated subject.
  // Return std::nullopt if the token is invalid or expired.
  std::optional<std::string> verify(const std::string& token) const;

 private:
  struct KeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
  };

  std::unique_ptr<EVP_PKEY, KeyDeleter> publicKey_;
};

}  // namespace messaging::security