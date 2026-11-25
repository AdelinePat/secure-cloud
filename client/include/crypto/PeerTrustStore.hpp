#pragma once

#include <QString>

#include "crypto/CryptoTypes.hpp"

namespace crypto {

class PeerTrustStore {
 public:
  enum class Status { Trusted, KeyMismatch, PeerMismatch };

  struct Result {
    Status status;
    QString expectedPeer;
    QString expectedFingerprint;
    QString presentedFingerprint;
  };

  static QString fingerprint(const PublicKey& key);

  // Checks and records a first encounter in one locked transaction.
  // Storage and validation errors throw. Existing pins are never overwritten.
  static Result checkOrPin(const QString& localUser, const QString& conversation,
                           const QString& peerUser, const PublicKey& key);
};

}  // namespace crypto
