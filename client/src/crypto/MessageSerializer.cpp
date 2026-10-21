#include "crypto/MessageSerializer.hpp"

#include <sodium.h>

#include <stdexcept>
#include <string>

namespace crypto {

std::string MessageSerializer::nonceToBase64(const Nonce& nonce) {
  std::size_t encodedLength =
      sodium_base64_ENCODED_LEN(nonce.size(), sodium_base64_VARIANT_ORIGINAL);

  std::string encodedNonce;
  encodedNonce.resize(encodedLength);

  sodium_bin2base64(encodedNonce.data(),  // destination
                    encodedLength,        // destination size
                    nonce.data(),         // source
                    nonce.size(),         // source size
                    sodium_base64_VARIANT_ORIGINAL);

  encodedNonce.resize(encodedLength - 1);

  return encodedNonce;
}

Nonce MessageSerializer::base64ToNonce(const std::string& encodedNonce) {
  Nonce nonce;

  size_t decodedLength;
  int result = sodium_base642bin(nonce.data(),         // destination
                                 nonce.size(),         // destination size
                                 encodedNonce.data(),  // source
                                 encodedNonce.size(),  // source size
                                 nullptr,              // ignore characters
                                 &decodedLength,       // output length
                                 nullptr,              // b64_end not needed
                                 sodium_base64_VARIANT_ORIGINAL);

  if (result != 0) {
    throw std::runtime_error("Failed to decode nonce from Base64");
  }

  if (decodedLength != nonce.size()) {
    throw std::runtime_error("Invalid nonce size");
  }

  return nonce;
}

std::string MessageSerializer::ciphertextToBase64(
    const Ciphertext& ciphertext) {
  std::size_t encodedLength = sodium_base64_ENCODED_LEN(
      ciphertext.size(), sodium_base64_VARIANT_ORIGINAL);

  std::string encodedCiphertext;
  encodedCiphertext.resize(encodedLength);

  sodium_bin2base64(encodedCiphertext.data(),  // destination
                    encodedLength,             // destination size
                    ciphertext.data(),         // source
                    ciphertext.size(),         // source size
                    sodium_base64_VARIANT_ORIGINAL);

  encodedCiphertext.resize(encodedLength - 1);

  return encodedCiphertext;
}

Ciphertext MessageSerializer::base64ToCiphertext(
    const std::string& encodedCiphertext) {
  Ciphertext ciphertext;

  std::size_t maxDecodedLength = (encodedCiphertext.size() / 4) * 3;

  ciphertext.resize(maxDecodedLength);

  size_t decodedLength;
  int result = sodium_base642bin(ciphertext.data(),         // destination
                                 ciphertext.size(),         // destination size
                                 encodedCiphertext.data(),  // source
                                 encodedCiphertext.size(),  // source size
                                 nullptr,                   // ignore characters
                                 &decodedLength,            // output length
                                 nullptr,  // b64_end not needed
                                 sodium_base64_VARIANT_ORIGINAL);

  if (result != 0) {
    throw std::runtime_error("Failed to decode ciphertext from Base64");
  }

  ciphertext.resize(decodedLength);

  return ciphertext;
}

}  // namespace crypto