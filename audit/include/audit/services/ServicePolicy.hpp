#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "audit/Errors.hpp"

namespace audit::services {

// MS-to-MS authorisation.
//
// With mTLS enabled the caller identity is the CN of its client certificate
// (CN=auth, CN=file, ...). A service may only append events it signed for:
// body.service_name must equal its certificate CN, so `file` cannot forge
// `login_failed` events attributed to `auth`.
struct ServicePolicy {
  std::vector<std::string> allowed_services;
  bool require_caller_identity = true;

  bool allows(std::string_view service) const {
    return std::any_of(allowed_services.begin(), allowed_services.end(),
                       [&](const std::string& allowed) {
                         return allowed == service;
                       });
  }

  void authorizeWriter(std::string_view caller,
                       std::string_view service_name) const {
    if (require_caller_identity) {
      if (caller.empty()) {
        throw forbiddenError("client certificate required");
      }
      if (caller != service_name) {
        throw forbiddenError(
            "service_name does not match the client certificate CN");
      }
    }
    if (!allows(service_name)) {
      throw forbiddenError("service is not allowed to emit audit events");
    }
  }

  void authorizeReader(std::string_view caller) const {
    if (!require_caller_identity) return;
    if (caller.empty()) {
      throw forbiddenError("client certificate required");
    }
    if (!allows(caller)) {
      throw forbiddenError("service is not allowed to read audit events");
    }
  }
};

}  // namespace audit::services
