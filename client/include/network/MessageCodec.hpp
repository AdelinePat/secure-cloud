#pragma once

#include <string>

#include "crypto/CryptoTypes.hpp"

namespace network
{

class MessageCodec
{
public:
    static std::string serialize(
        const crypto::EncryptedMessage& encryptedMessage
    );

    static crypto::EncryptedMessage deserialize(
        const std::string& json
    );
};

} // namespace network