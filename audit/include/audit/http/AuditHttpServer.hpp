#pragma once

#include <memory>
#include <string>

#include "audit/http/HttpConfig.hpp"
#include "audit/services/AuditService.hpp"

namespace audit::http {

// Internal, MS-to-MS HTTP(S) front end for the audit service.
//
// Routes (all under /audit, plus /health):
//   POST /audit/events            append one event
//   POST /audit/events/batch      append several events in order
//   GET  /audit/events            query with filters + keyset pagination
//   GET  /audit/events/{id}       fetch one event
//   GET  /audit/verify            verify the whole hash chain
//   GET  /health                  liveness + Mongo reachability
//
// This service is deliberately not exposed through the API gateway: only
// other microservices talk to it, over the internal network and mTLS.
class AuditHttpServer {
 public:
  AuditHttpServer(HttpConfig config, services::AuditService& service);
  ~AuditHttpServer();

  AuditHttpServer(const AuditHttpServer&) = delete;
  AuditHttpServer& operator=(const AuditHttpServer&) = delete;

  // Blocks until stop() is called. Returns false if the socket could not be
  // bound or TLS configuration is invalid.
  bool run();

  // Test hooks: bind on an ephemeral port, then serve in a background thread.
  int bindToAnyPort(const std::string& host);
  bool listenAfterBind();

  void stop();
  bool is_valid() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  HttpConfig config_;
  services::AuditService& service_;
};

}  // namespace audit::http
