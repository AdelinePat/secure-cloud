#pragma once

#include <stdexcept>
#include <string>

namespace audit {

// Error kinds map 1:1 to HTTP status codes in the HTTP layer:
//   Validation -> 400, Forbidden -> 403, NotFound -> 404,
//   Conflict -> 409, Storage -> 503.
enum class ErrorKind { Validation, Forbidden, NotFound, Conflict, Storage };

class AuditError : public std::runtime_error {
 public:
  AuditError(ErrorKind kind, const std::string& message)
      : std::runtime_error(message), kind_(kind) {}

  ErrorKind kind() const noexcept { return kind_; }

 private:
  ErrorKind kind_;
};

inline AuditError validationError(const std::string& message) {
  return AuditError(ErrorKind::Validation, message);
}

inline AuditError forbiddenError(const std::string& message) {
  return AuditError(ErrorKind::Forbidden, message);
}

inline AuditError notFoundError(const std::string& message) {
  return AuditError(ErrorKind::NotFound, message);
}

inline AuditError conflictError(const std::string& message) {
  return AuditError(ErrorKind::Conflict, message);
}

inline AuditError storageError(const std::string& message) {
  return AuditError(ErrorKind::Storage, message);
}

}  // namespace audit
