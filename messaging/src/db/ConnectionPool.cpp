#include "messaging/db/ConnectionPool.hpp"

#include <stdexcept>
#include <utility>

namespace messaging::db {

ConnectionPool::Lease::Lease(ConnectionPool& pool,
                             std::unique_ptr<pqxx::connection> connection)
    : pool_(&pool), connection_(std::move(connection)) {}

ConnectionPool::Lease::Lease(Lease&& other) noexcept
    : pool_(std::exchange(other.pool_, nullptr)),
      connection_(std::move(other.connection_)) {}

ConnectionPool::Lease::~Lease() {
  if (pool_ != nullptr) {
    pool_->release(std::move(connection_));
  }
}

ConnectionPool::ConnectionPool(std::string connectionString, std::size_t size)
    : connectionString_(std::move(connectionString)), size_(size) {
  if (size_ == 0) {
    throw std::invalid_argument("ConnectionPool size must be at least 1");
  }
  idle_.reserve(size_);
  for (std::size_t i = 0; i < size_; ++i) {
    idle_.push_back(std::make_unique<pqxx::connection>(connectionString_));
  }
}

ConnectionPool::Lease ConnectionPool::acquire() {
  std::unique_ptr<pqxx::connection> connection;
  {
    std::unique_lock lock(mutex_);
    available_.wait(lock, [this] { return !idle_.empty(); });
    connection = std::move(idle_.back());
    idle_.pop_back();
  }

  if (!connection || !connection->is_open()) {
    try {
      connection = std::make_unique<pqxx::connection>(connectionString_);
    } catch (...) {
      release(nullptr);  // keep the slot so the pool does not shrink
      throw;
    }
  }
  return Lease(*this, std::move(connection));
}

void ConnectionPool::release(std::unique_ptr<pqxx::connection> connection) {
  {
    std::lock_guard lock(mutex_);
    idle_.push_back(std::move(connection));
  }
  available_.notify_one();
}

}  // namespace messaging::db
