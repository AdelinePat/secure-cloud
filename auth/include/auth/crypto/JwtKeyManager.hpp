
#pragma once

#include <openssl/evp.h>

#include <memory>
#include <string>

namespace auth::crypto {

class JwtKeyManager {
 public:
  // Automatically release OpenSSL keys when they are no longer needed.
  struct KeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
  };

  using KeyPtr = std::unique_ptr<EVP_PKEY, KeyDeleter>;

  // Load an existing ES256 key pair from PEM files.
  JwtKeyManager(const std::string& privateKeyPath,
                const std::string& publicKeyPath);

  // Provide access to the loaded keys without transferring ownership.
  EVP_PKEY* privateKey() const noexcept;
  EVP_PKEY* publicKey() const noexcept;

 private:
  KeyPtr privateKey_;
  KeyPtr publicKey_;
};

}  // namespace auth::crypto
