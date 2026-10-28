#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "messaging/domain/Message.hpp"

namespace messaging::repositories {

struct MessagePage {
  std::vector<domain::Message> messages;   // newest first
  std::optional<std::string> nextCursor;   // pass as `before` to fetch the next page
};

// Thrown when a message id is inserted twice (maps to the API's 409).
class DuplicateMessageError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class IMessageRepository {
 public:
  virtual ~IMessageRepository() = default;

  // Throws DuplicateMessageError on duplicate id.
  virtual domain::Message create(const domain::Message& message) = 0;

  // `before`: opaque cursor from a previous page's nextCursor; nullopt for the
  // most recent page.
  virtual MessagePage history(const std::string& conversationId,
                               std::optional<std::string> before,
                               int limit) = 0;
};

}  // namespace messaging::repositories
