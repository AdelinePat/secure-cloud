#include "crypto/KeyManager.hpp"

#include <sodium.h>

namespace crypto
{

KeyPair KeyManager::generateKeyPair()
{
    KeyPair keyPair;

    crypto_kx_keypair(
        keyPair.publicKey.data(),
        keyPair.privateKey.data()
    );

    return keyPair;
}

}