#pragma once

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <vector>

#include "messaging/repositories/IMessageRepository.hpp"

namespace messaging::repositories::test {

class InMemoryMessageRepository : public IMessageRepository {
 public:
  domain::Message create(const domain::Message& message) override {
    for (const auto& m : messages_) {
      if (m.id == message.id) {
        throw DuplicateMessageError("duplicate message id");
      }
    }
    domain::Message stored = message;
    stored.server_timestamp = nextTimestamp();
    messages_.push_back(stored);
    return stored;
  }

  MessagePage history(const std::string& conversationId,
                       std::optional<std::string> before,
                       int limit) override {
    std::vector<domain::Message> matching;
    for (const auto& m : messages_) {
      if (m.conversation_id != conversationId) continue;

      if (before.has_value()) {
        const auto [timestamp, id] = parseCursor(*before);
        if (!(m.server_timestamp < timestamp ||
              (m.server_timestamp == timestamp && m.id < id))) {
          continue;
        }
      }

      matching.push_back(m);
    }

    // newest first
    std::sort(matching.begin(), matching.end(),
              [](const domain::Message& a, const domain::Message& b) {
                if (a.server_timestamp != b.server_timestamp) {
                  return a.server_timestamp > b.server_timestamp;
                }
                return a.id > b.id;
              });

    MessagePage page;
    for (int i = 0; i < static_cast<int>(matching.size()) && i < limit; ++i) {
      page.messages.push_back(matching[i]);
    }
    if (static_cast<int>(page.messages.size()) == limit &&
        !page.messages.empty()) {
      page.nextCursor = page.messages.back().server_timestamp + "|" +
                        page.messages.back().id;
    }
    return page;
  }

 private:
  std::vector<domain::Message> messages_;
  int counter_ = 0;

  static std::pair<std::string, std::string> parseCursor(
      const std::string& cursor) {
    const auto separator = cursor.find('|');
    if (separator == std::string::npos) {
      return {cursor, "ffffffff-ffff-ffff-ffff-ffffffffffff"};
    }
    return {cursor.substr(0, separator), cursor.substr(separator + 1)};
  }

  // Fake but strictly increasing "timestamps" so ordering is deterministic in
  // tests.
  std::string nextTimestamp() {
    std::ostringstream oss;
    oss << "1970-01-01T00:00:" << std::setw(6) << std::setfill('0') << counter_++
        << "Z";
    return oss.str();
  }
};

}  // namespace messaging::repositories::test
