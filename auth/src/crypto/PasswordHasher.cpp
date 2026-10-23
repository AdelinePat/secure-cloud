#include "auth/crypto/PasswordHasher.hpp"

#include <sodium.h>

#include <stdexcept>

namespace auth::crypto {

std::string PasswordHasher::hash(const std::string& password) {
  char hashedPassword[crypto_pwhash_STRBYTES];

  if (crypto_pwhash_str_alg(hashedPassword, password.c_str(), password.size(),
                            crypto_pwhash_OPSLIMIT_INTERACTIVE,
                            crypto_pwhash_MEMLIMIT_INTERACTIVE,
                            crypto_pwhash_ALG_ARGON2ID13) != 0) {
    throw std::runtime_error("Password hashing failed");
  }

  return std::string(hashedPassword);
}

bool PasswordHasher::verify(const std::string& password,
                            const std::string& storedHash) {
  return crypto_pwhash_str_verify(storedHash.c_str(), password.c_str(),
                                  password.size()) == 0;
}

}  // namespace auth::crypto
