#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include "audit/config/AuditConfig.hpp"
#include "audit/http/AuditHttpServer.hpp"
#include "audit/repositories/MongoAuditRepository.hpp"
#include "audit/services/AuditService.hpp"

namespace {

std::atomic<bool> g_stop_requested{false};

void handleSignal(int) { g_stop_requested.store(true); }

}  // namespace

int main() {
  using namespace audit;

  try {
    const auto config = config::AuditConfig::fromEnv();
    config::AuditConfig::logSummary(config);

    repositories::MongoAuditRepository repository(config.mongo);
    repository.ensureIndexes();

    services::AuditService service(repository, config.policy);
    http::AuditHttpServer server(config.http, service);

    if (!server.is_valid()) {
      std::cerr << "[audit] server configuration is invalid\n";
      return 1;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // cpp-httplib blocks in listen(); stop it from a separate thread so
    // SIGTERM/SIGINT shut the process down cleanly instead of killing it
    // mid-request.
    std::thread watcher([&server] {
      while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
      server.stop();
    });

    std::cout << "[audit] listening on " << config.http.host << ':'
              << config.http.port << std::endl;
    const bool listening = server.run();
    g_stop_requested.store(true);
    watcher.join();

    if (!listening) {
      std::cerr << "[audit] could not bind the HTTP socket\n";
      return 1;
    }

    std::cout << "[audit] stopped" << std::endl;
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[audit] fatal: " << exception.what() << std::endl;
    return 1;
  }
}
