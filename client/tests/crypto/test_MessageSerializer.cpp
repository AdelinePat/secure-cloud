#include <sodium.h>

#include "crypto/MessageSerializer.hpp"

int main()
{
    if (sodium_init() < 0)
    {
        return 1;
    }

    crypto::Nonce originalNonce;

    randombytes_buf(originalNonce.data(), originalNonce.size());

    std::string encodedNonce = crypto::MessageSerializer::nonceToBase64(originalNonce);

    crypto::Nonce reconstructedNonce = crypto::MessageSerializer::base64ToNonce(encodedNonce);

    if (originalNonce != reconstructedNonce)
    {
        return 1;
    }

    crypto::Ciphertext originalCiphertext;
    
    originalCiphertext.resize(100);

    randombytes_buf(originalCiphertext.data(), originalCiphertext.size());

    std::string encodedCiphertext = crypto::MessageSerializer::ciphertextToBase64(originalCiphertext);

    crypto::Ciphertext reconstructedCiphertext = crypto::MessageSerializer::base64ToCiphertext(encodedCiphertext);

    if (originalCiphertext != reconstructedCiphertext)
    {
        return 1;
    }

    return 0;
}