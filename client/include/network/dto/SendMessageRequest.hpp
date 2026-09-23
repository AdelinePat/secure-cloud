#pragma once

#include <string>

#include "crypto/CryptoTypes.hpp"

namespace network::dto {

struct SendMessageRequest
{
    std::string messageId;

    crypto::EncryptedMessage encryptedMessage;

    int encryptionVersion = 1;

    std::string clientTimestamp;
};

} // namespace network::dto