#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>
#include <string>
#include <vector>

namespace messaging::db {

// Fixed-size pool of PostgreSQL connections. A pqxx::connection must not be
// used by two threads at once, so each request borrows one for its duration.
//
// All connections are opened in the constructor, so a wrong configuration
// fails at startup. A connection found closed when borrowed (database
// restart, network cut) is reopened then.
class ConnectionPool {
public:
  // Returns its connection to the pool when destroyed.
  class Lease {
  public:
    Lease(Lease &&other) noexcept;
    Lease &operator=(Lease &&) = delete;
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    ~Lease();

    pqxx::connection &connection() { return *connection_; }

  private:
    friend class ConnectionPool;
    Lease(ConnectionPool &pool, std::unique_ptr<pqxx::connection> connection);

    ConnectionPool *pool_;
    std::unique_ptr<pqxx::connection> connection_;
  };

  ConnectionPool(std::string connectionString, std::size_t size);

  // Blocks until a connection is free. Throws if a closed connection cannot
  // be reopened (the slot stays in the pool and is retried next time).
  Lease acquire();

  std::size_t size() const { return size_; }

private:
  void release(std::unique_ptr<pqxx::connection> connection);

  const std::string connectionString_;
  const std::size_t size_;
  std::mutex mutex_;
  std::condition_variable available_;
  // Free slots. A null entry is a connection that failed to reopen.
  std::vector<std::unique_ptr<pqxx::connection>> idle_;
};

} // namespace messaging::db
