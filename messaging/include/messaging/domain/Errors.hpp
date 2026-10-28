#pragma once

#include <stdexcept>
#include <string>

namespace messaging::domain {

// Business error carrying the HTTP status the API should return.
// The router translates it to a JSON error response.
class MessagingError : public std::runtime_error {
 public:
  MessagingError(int status, std::string code, const std::string& message)
      : std::runtime_error(message),
        status_(status),
        code_(std::move(code)) {}

  int status() const noexcept { return status_; }
  const std::string& code() const noexcept { return code_; }

 private:
  int status_;
  std::string code_;
};

}  // namespace messaging::domain
