#pragma once

#include <optional>
#include <string>

namespace auth::domain {

struct Session {
  std::string id;                    // empty when constructing a new session to pass to create()
  std::string user_id;
  std::string refresh_token_hash;    // hash of the refresh token, never the raw token
  std::string issued_at;             // ISO8601
  std::string expires_at;            // ISO8601
  std::optional<std::string> revoked_at;  // set once revoked, otherwise nullopt
};

}  // namespace auth::domain
