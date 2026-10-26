#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "audit/repositories/IAuditRepository.hpp"

namespace audit::tests {

// Test double with the same collision semantics as the Mongo repository:
// `id`, `seq` and `prev_hash` are unique. Kept deliberately simple; the chain
// logic itself is tested through AuditService.
class InMemoryAuditRepository final : public repositories::IAuditRepository {
 public:
  void insert(const domain::AuditEvent& event) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [seq, existing] : events_) {
      if (existing.id == event.id || existing.seq == event.seq ||
          existing.prev_hash == event.prev_hash) {
        throw repositories::DuplicateKeyError();
      }
    }
    events_[event.seq] = event;
  }

  std::optional<domain::AuditEvent> findById(const std::string& id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [seq, event] : events_) {
      if (event.id == id) return event;
    }
    return std::nullopt;
  }

  std::optional<domain::AuditEvent> latest() override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.empty()) return std::nullopt;
    return events_.rbegin()->second;
  }

  std::vector<domain::AuditEvent> find(const domain::AuditQuery& query) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<domain::AuditEvent> result;

    const auto matches = [&](const domain::AuditEvent& event) {
      if (query.event_type.has_value() && event.event_type != *query.event_type) {
        return false;
      }
      if (query.service_name.has_value() &&
          event.service_name != *query.service_name) {
        return false;
      }
      if (query.actor_id.has_value() && event.actor_id != query.actor_id) {
        return false;
      }
      if (query.target_id.has_value() && event.target_id != query.target_id) {
        return false;
      }
      if (query.outcome.has_value() && event.outcome != *query.outcome) {
        return false;
      }
      if (query.from.has_value() && event.occurred_at < *query.from) return false;
      if (query.to.has_value() && event.occurred_at > *query.to) return false;
      if (query.before_seq.has_value() && event.seq >= *query.before_seq) {
        return false;
      }
      if (query.after_seq.has_value() && event.seq <= *query.after_seq) {
        return false;
      }
      return true;
    };

    for (const auto& [seq, event] : events_) {
      if (matches(event)) result.push_back(event);
    }
    if (!query.ascending) std::reverse(result.begin(), result.end());
    if (query.limit >= 0 && result.size() > static_cast<std::size_t>(query.limit)) {
      result.resize(static_cast<std::size_t>(query.limit));
    }
    return result;
  }

  void forEachOrdered(
      const std::function<bool(const domain::AuditEvent&)>& visitor) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [seq, event] : events_) {
      if (!visitor(event)) return;
    }
  }

  int64_t count() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int64_t>(events_.size());
  }

  bool healthy() override { return true; }

  // ── test helpers ────────────────────────────────────────────────────────
  std::vector<domain::AuditEvent> all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<domain::AuditEvent> result;
    for (const auto& [seq, event] : events_) result.push_back(event);
    return result;
  }

  void overwrite(const domain::AuditEvent& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_[event.seq] = event;
  }

  void erase(int64_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.erase(seq);
  }

 private:
  mutable std::mutex mutex_;
  std::map<int64_t, domain::AuditEvent> events_;
};

}  // namespace audit::tests
