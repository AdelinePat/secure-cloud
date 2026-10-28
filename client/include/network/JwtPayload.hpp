#pragma once

#include <QString>

namespace network {

class JwtPayload {
 public:
  // Extract the `sub` claim without verifying the signature. This is only used
  // by the UI to distinguish the current user's messages; the server remains
  // the authority on token validity.
  static QString subject(const QString& jwt);
};

}  // namespace network
