#pragma once

#include "crypto/CryptoTypes.hpp"

namespace crypto
{

class KeyManager
{
public:
    static KeyPair generateKeyPair();
    
    static SessionKeys deriveClientSessionKeys(
        const PublicKey& ownPublicKey,
        const PrivateKey& ownPrivateKey,
        const PublicKey& peerPublicKey);

    static SessionKeys deriveServerSessionKeys(
        const PublicKey& ownPublicKey,
        const PrivateKey& ownPrivateKey,
        const PublicKey& peerPublicKey);
};

}