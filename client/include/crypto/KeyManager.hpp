#pragma once

#include "crypto/CryptoTypes.hpp"

namespace crypto
{

class KeyManager
{
public:
    static KeyPair generateKeyPair();
};

}