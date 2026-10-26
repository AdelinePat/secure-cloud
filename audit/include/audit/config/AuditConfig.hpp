#pragma once

#include "audit/http/HttpConfig.hpp"
#include "audit/repositories/MongoAuditRepository.hpp"
#include "audit/services/ServicePolicy.hpp"

namespace audit::config {

struct AuditConfig {
  repositories::MongoConfig mongo;
  http::HttpConfig http;
  services::ServicePolicy policy;
  bool require_caller_identity = true;

  // Reads AUDIT_*, MONGO_* from the environment. Missing values fall back to
  // local-development defaults; secrets are never logged.
  static AuditConfig fromEnv();

  static void logSummary(const AuditConfig& config);
};

}  // namespace audit::config
