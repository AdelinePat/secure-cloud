#include <sodium.h>

#include "network/MessageCodec.hpp"

int main()
{
    if (sodium_init() < 0)
    {
        return 1;
    }

    crypto::EncryptedMessage encryptedMessage;

    randombytes_buf(
        encryptedMessage.nonce.data(),
        encryptedMessage.nonce.size()
    );

    encryptedMessage.ciphertext.resize(100);

    randombytes_buf(
        encryptedMessage.ciphertext.data(),
        encryptedMessage.ciphertext.size()
    );

    std::string serializedMessage =
        network::MessageCodec::serialize(encryptedMessage);

    crypto::EncryptedMessage deserializedMessage =
        network::MessageCodec::deserialize(serializedMessage);

    if (encryptedMessage.nonce != deserializedMessage.nonce ||
        encryptedMessage.ciphertext != deserializedMessage.ciphertext)
    {
        return 1;
    }

    return 0;
}