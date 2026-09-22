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

    crypto::Nonce reconstructedNonce = crypto::MessageSerializer::base64ToNonce(crypto::MessageSerializer::nonceToBase64(originalNonce));

    if (originalNonce != reconstructedNonce)
    {
        return 1;
    }

    return 0;
}