#include <sodium.h>
#include <cstdio>
#include <cstring>
#include <string>

int main() {
  if (sodium_init() < 0) {
    std::fprintf(stderr, "sodium_init failed\n");
    return 1;
  }

  const char* passwords[] = {"password123", "password456", "password789"};
  const char* names[] = {"alice", "bob", "charlie"};

  for (int i = 0; i < 3; ++i) {
    char hashed[crypto_pwhash_STRBYTES];

    int rc = crypto_pwhash_str(
        hashed, passwords[i], std::strlen(passwords[i]),
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE);

    if (rc != 0) {
      std::fprintf(stderr, "hashing failed for %s\n", names[i]);
      return 1;
    }

    std::printf("%s: %s\n", names[i], hashed);
  }

  return 0;
}
