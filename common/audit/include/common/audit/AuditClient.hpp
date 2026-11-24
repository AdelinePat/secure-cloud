#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace common::audit {

// Environment-driven configuration for the emitter.
//
//   AUDIT_ENABLED           false by default; Compose sets it to true
//   AUDIT_URL               https://audit:8085 by default (http:// also works)
//   AUDIT_SERVICE_NAME      defaults to the service passed to fromEnv()
//   AUDIT_TLS_CERT          /app/certs/<service>/<service>.crt by default
//   AUDIT_TLS_KEY           /app/certs/<service>/<service>.key by default
//   AUDIT_TLS_CA            /app/certs/ca/ca.crt by default
//   AUDIT_TIMEOUT_MS        750 by default (per HTTP step)
//   AUDIT_SPOOL_DIR         empty by default = in-memory queue (not durable)
//   AUDIT_SPOOL_MAX_EVENTS  10000 by default; emit() refuses beyond that
//   AUDIT_RETRY_INITIAL_MS  200 by default (first back-off after a failure)
//   AUDIT_RETRY_MAX_MS      30000 by default (back-off cap)
//   AUDIT_SHUTDOWN_DRAIN_MS 2000 by default (delivery time left at shutdown)
struct Config {
  bool enabled = false;
  std::string url = "https://audit:8085";
  std::string service_name;
  std::string cert_path;
  std::string key_path;
  std::string ca_path;
  int timeout_ms = 750;

  std::string spool_dir;
  std::size_t max_pending = 10000;
  int retry_initial_ms = 200;
  int retry_max_ms = 30000;
  int shutdown_drain_ms = 2000;

  static Config fromEnv(std::string default_service_name);
};

// One audit event, as provided by a microservice. The store owns id, seq,
// recorded_at, prev_hash and hash. occurred_at defaults to the emit() time.
struct Event {
  std::string event_type;
  std::optional<std::string> actor_id;
  std::optional<std::string> target_id;
  std::string outcome = "success";
  std::optional<std::string> occurred_at;
  nlohmann::json metadata = nlohmann::json::object();
};

// Asynchronous, durable emitter for the audit service.
//
// emit() gives the event an event_id and occurred_at, writes it to the spool
// directory (write temp file, fsync, rename, fsync dir) and returns. A single
// background worker delivers spooled events in order over mTLS:
//
//   200/201                  delivered, spool file removed
//   transport error, 408,    kept at the head of the queue and retried with
//   409, 429, 5xx            the same event_id (idempotent on the server),
//                            exponential back-off up to AUDIT_RETRY_MAX_MS
//   other 4xx                moved to <spool>/dead/ and logged, so one bad
//                            event cannot block the queue
//
// Events left in the spool (audit down, crash, restart) are re-sent by the
// next Client that opens the same directory. The directory is locked with
// flock(): one process per spool directory.
//
// Without AUDIT_SPOOL_DIR (or if it cannot be opened/locked) the queue is kept
// in memory: delivery is still asynchronous and retried, but pending events
// are lost if the process stops.
//
// emit() never throws: a failed audit call must not fail the business request.
// It returns false only when the event is invalid or the queue is full.
class Client {
 public:
  explicit Client(Config config);
  ~Client();

  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  bool enabled() const;
  bool durable() const;
  const std::string& serviceName() const;

  // Accepts the event into the queue (and the spool, when durable).
  bool emit(const Event& event);

  // Number of accepted events not yet delivered or dead-lettered.
  std::size_t pending() const;

  // Waits until the queue is empty or the timeout expires. True when empty.
  bool flush(std::chrono::milliseconds timeout);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace common::audit
