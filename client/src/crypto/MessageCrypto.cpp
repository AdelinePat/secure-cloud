#include "crypto/MessageCrypto.hpp"

#include <sodium.h>

#include <stdexcept>

namespace crypto {

EncryptedMessage MessageCrypto::encrypt(const std::string& message,
                                        const SessionKey& key) {
  EncryptedMessage encryptedMessage;

  // 1. Generate Nonce
  randombytes_buf(encryptedMessage.nonce.data(), encryptedMessage.nonce.size());

  // 2. Size ciphertext
  encryptedMessage.ciphertext.resize(message.size() +
                                     crypto_secretbox_MACBYTES);

  // 3. Encrypt message
  int result = crypto_secretbox_easy(
      encryptedMessage.ciphertext.data(),
      reinterpret_cast<const unsigned char*>(message.data()), message.size(),
      encryptedMessage.nonce.data(), key.data());

  if (result != 0) {
    throw std::runtime_error("Failed to encrypt message");
  }

  return encryptedMessage;
}

std::string MessageCrypto::decrypt(
    const EncryptedMessage& encryptedMessage,
    const SessionKey& key)
{
     // 4. Decrypt message
  std::string decryptedMessage;

  if(encryptedMessage.ciphertext.size() < crypto_secretbox_MACBYTES) {
    throw std::runtime_error("Ciphertext is too short to contain a valid MAC");
  }

  decryptedMessage.resize(encryptedMessage.ciphertext.size() - crypto_secretbox_MACBYTES);

  int result = crypto_secretbox_open_easy(
      reinterpret_cast<unsigned char*>(decryptedMessage.data()),
      encryptedMessage.ciphertext.data(),
      encryptedMessage.ciphertext.size(), 
      encryptedMessage.nonce.data(),
      key.data());

  if (result != 0) {
    throw std::runtime_error("Failed to decrypt message");
  }

  return decryptedMessage;
}

}  // namespace crypto