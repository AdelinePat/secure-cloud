#include "audit/config/AuditConfig.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace audit::config {
namespace {

std::string env(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  if (value == nullptr || value[0] == '\0') return fallback;
  return value;
}

int envInt(const char* name, int fallback) {
  const std::string raw = env(name, "");
  if (raw.empty()) return fallback;
  try {
    const int value = std::stoi(raw);
    return value;
  } catch (const std::exception&) {
    throw std::runtime_error(std::string(name) + " must be an integer");
  }
}

bool envBool(const char* name, bool fallback) {
  std::string raw = env(name, "");
  if (raw.empty()) return fallback;
  std::transform(raw.begin(), raw.end(), raw.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (raw == "1" || raw == "true" || raw == "yes" || raw == "on") return true;
  if (raw == "0" || raw == "false" || raw == "no" || raw == "off") return false;
  throw std::runtime_error(std::string(name) + " must be a boolean");
}

std::vector<std::string> envList(const char* name, const std::string& fallback) {
  std::vector<std::string> values;
  std::stringstream stream(env(name, fallback));
  std::string item;
  while (std::getline(stream, item, ',')) {
    const auto first = item.find_first_not_of(" \t");
    const auto last = item.find_last_not_of(" \t");
    if (first == std::string::npos) continue;
    values.push_back(item.substr(first, last - first + 1));
  }
  return values;
}

}  // namespace

AuditConfig AuditConfig::fromEnv() {
  AuditConfig config;

  config.mongo.uri = env("MONGO_URI", "");
  config.mongo.host = env("MONGO_HOST", "localhost");
  config.mongo.port = envInt("MONGO_PORT", 27017);
  config.mongo.database = env("MONGO_DB", "auditDB");
  config.mongo.username = env("MONGO_USER", "");
  config.mongo.password = env("MONGO_PASSWORD", "");
  config.mongo.auth_source = env("MONGO_AUTH_SOURCE", config.mongo.database);
  config.mongo.collection = env("MONGO_AUDIT_COLLECTION", "auditLogs");

  config.http.host = env("AUDIT_HOST", "0.0.0.0");
  config.http.port = envInt("AUDIT_PORT", 8085);
  config.http.tls_enabled = envBool("AUDIT_TLS_ENABLED", false);
  config.http.cert_path = env("AUDIT_TLS_CERT", "/app/certs/audit/audit.crt");
  config.http.key_path = env("AUDIT_TLS_KEY", "/app/certs/audit/audit.key");
  config.http.ca_path = env("AUDIT_TLS_CA", "/app/certs/ca/ca.crt");
  config.http.max_payload_bytes = static_cast<std::size_t>(
      envInt("AUDIT_MAX_PAYLOAD_BYTES", 1024 * 1024));

  config.require_caller_identity =
      envBool("AUDIT_REQUIRE_CALLER_IDENTITY", config.http.tls_enabled);
  config.policy.require_caller_identity = config.require_caller_identity;
  config.policy.allowed_services =
      envList("AUDIT_ALLOWED_SERVICES", "auth,file,messaging,gateway");

  if (config.http.port <= 0 || config.http.port > 65535) {
    throw std::runtime_error("AUDIT_PORT must be between 1 and 65535");
  }
  if (config.mongo.port <= 0 || config.mongo.port > 65535) {
    throw std::runtime_error("MONGO_PORT must be between 1 and 65535");
  }
  if (config.http.tls_enabled &&
      (config.http.cert_path.empty() || config.http.key_path.empty() ||
       config.http.ca_path.empty())) {
    throw std::runtime_error(
        "AUDIT_TLS_CERT, AUDIT_TLS_KEY and AUDIT_TLS_CA are required when TLS is enabled");
  }

  return config;
}

void AuditConfig::logSummary(const AuditConfig& config) {
  std::cout << "[audit] http=" << config.http.host << ':' << config.http.port
            << " tls=" << (config.http.tls_enabled ? "on" : "off")
            << " mongo=" << (config.mongo.uri.empty()
                                 ? config.mongo.host + ":" +
                                       std::to_string(config.mongo.port)
                                 : "<MONGO_URI>")
            << " db=" << config.mongo.database
            << " collection=" << config.mongo.collection
            << " user=" << (config.mongo.username.empty() ? "<none>" : "<set>")
            << " require_caller_identity="
            << (config.require_caller_identity ? "true" : "false")
            << " allowed_services=";
  for (std::size_t i = 0; i < config.policy.allowed_services.size(); ++i) {
    if (i != 0) std::cout << ',';
    std::cout << config.policy.allowed_services[i];
  }
  std::cout << std::endl;
}

}  // namespace audit::config
