#pragma once

#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace common::audit {

// Environment-driven configuration for the emitter.
//
//   AUDIT_ENABLED      false by default; Compose sets it to true
//   AUDIT_URL          https://audit:8085 by default (http:// also works)
//   AUDIT_SERVICE_NAME defaults to the service passed to fromEnv()
//   AUDIT_TLS_CERT     /app/certs/<service>/<service>.crt by default
//   AUDIT_TLS_KEY      /app/certs/<service>/<service>.key by default
//   AUDIT_TLS_CA       /app/certs/ca/ca.crt by default
//   AUDIT_TIMEOUT_MS   750 by default
//   AUDIT_MAX_ATTEMPTS 2 by default
struct Config {
  bool enabled = false;
  std::string url = "https://audit:8085";
  std::string service_name;
  std::string cert_path;
  std::string key_path;
  std::string ca_path;
  int timeout_ms = 750;
  int max_attempts = 2;

  static Config fromEnv(std::string default_service_name);
};

// One audit event, as provided by a microservice. The store owns id, seq,
// recorded_at, prev_hash and hash; occurred_at defaults to "now" on the server.
struct Event {
  std::string event_type;
  std::optional<std::string> actor_id;
  std::optional<std::string> target_id;
  std::string outcome = "success";
  std::optional<std::string> occurred_at;
  nlohmann::json metadata = nlohmann::json::object();
};

// Best-effort, synchronous mTLS emitter for the audit service.
//
// emit() never throws: a failed audit call must not fail the business request.
// It retries transport errors and 5xx/409 responses with the same event_id, so
// a retry is idempotent on the server. There is no durable spool: if the audit
// service stays unreachable, the event is lost and an error is logged.
//
// This is deliberately a blocking client. It is cheap enough for the current
// synchronous servers and keeps the failure mode visible; a queue/worker can be
// added behind the same interface later.
class Client {
 public:
  explicit Client(Config config);
  ~Client();

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool enabled() const;
  const std::string& serviceName() const;

  bool emit(const Event& event);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace common::audit
