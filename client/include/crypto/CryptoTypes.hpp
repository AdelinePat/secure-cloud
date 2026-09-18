#pragma once

#include <array>
#include <sodium.h>

namespace crypto {
    using PublicKey = std::array<unsigned char, crypto_kx_PUBLICKEYBYTES>;

    using PrivateKey = std::array<unsigned char, crypto_kx_SECRETKEYBYTES>;

    using SessionKey = std::array<unsigned char, crypto_kx_SESSIONKEYBYTES>;

    struct KeyPair
    {
        PublicKey publicKey;
        PrivateKey privateKey;
    };
}