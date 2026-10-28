#pragma once

#include <openssl/evp.h>

#include <memory>
#include <optional>
#include <string>

namespace messaging::crypto {

// Verifies the ES256 access tokens issued by the auth service and returns the
// token subject (the user id). Messaging only needs the public key.
class IJwtVerifier {
 public:
  virtual ~IJwtVerifier() = default;

  virtual std::optional<std::string> verify(const std::string& token) const = 0;
};

class JwtVerifier : public IJwtVerifier {
 public:
  explicit JwtVerifier(const std::string& publicKeyPath);

  std::optional<std::string> verify(const std::string& token) const override;

 private:
  struct KeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
  };

  std::unique_ptr<EVP_PKEY, KeyDeleter> publicKey_;
};

}  // namespace messaging::crypto
