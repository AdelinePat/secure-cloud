#include "audit/services/AuditService.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "audit/Errors.hpp"
#include "audit/crypto/HashChain.hpp"
#include "audit/json/Json.hpp"
#include "audit/util/Time.hpp"
#include "audit/util/Uuid.hpp"

namespace audit::services {
namespace {

constexpr std::size_t kMaxEventType = 64;
constexpr std::size_t kMaxServiceName = 64;
constexpr std::size_t kMaxIdentifier = 128;
constexpr std::size_t kMaxTimestamp = 40;
constexpr std::size_t kMaxMetadataBytes = 16 * 1024;
constexpr int kMaxAppendAttempts = 8;
constexpr int kDefaultQueryLimit = 50;
constexpr int kMaxQueryLimit = 500;

bool isLowerToken(std::string_view value, std::size_t max_length) {
  if (value.empty() || value.size() > max_length)
    return false;
  return std::all_of(value.begin(), value.end(), [](char raw) {
    const auto c = static_cast<unsigned char>(raw);
    return std::islower(c) != 0 || std::isdigit(c) != 0 || c == '_' ||
           c == '.' || c == ':' || c == '-';
  });
}

bool isServiceName(std::string_view value) {
  if (value.empty() || value.size() > kMaxServiceName)
    return false;
  return std::all_of(value.begin(), value.end(), [](char raw) {
    const auto c = static_cast<unsigned char>(raw);
    return std::isalnum(c) != 0 || c == '_' || c == '.' || c == '-';
  });
}

bool isFreeIdentifier(std::string_view value, std::size_t max_length) {
  if (value.empty() || value.size() > max_length)
    return false;
  return std::all_of(value.begin(), value.end(), [](char raw) {
    const auto c = static_cast<unsigned char>(raw);
    return c >= 0x20 && c != 0x7F;
  });
}

bool looksLikeIso8601(std::string_view value) {
  // Shape check only: YYYY-MM-DDTHH:MM:SS, optional fraction/offset.
  if (value.size() < 19 || value.size() > kMaxTimestamp)
    return false;
  return std::isdigit(static_cast<unsigned char>(value[0])) != 0 &&
         std::isdigit(static_cast<unsigned char>(value[1])) != 0 &&
         std::isdigit(static_cast<unsigned char>(value[2])) != 0 &&
         std::isdigit(static_cast<unsigned char>(value[3])) != 0 &&
         value[4] == '-' && value[7] == '-' && value[10] == 'T' &&
         value[13] == ':' && value[16] == ':';
}

void require(bool condition, const std::string &message) {
  if (!condition)
    throw validationError(message);
}

} // namespace

AuditService::AuditService(repositories::IAuditRepository &repository,
                           ServicePolicy policy)
    : repository_(repository), policy_(std::move(policy)) {}

void AuditService::normalize(domain::AuditEvent &event) const {
  require(isLowerToken(event.event_type, kMaxEventType),
          "event_type must be 1-64 chars of [a-z0-9_.:-]");
  require(isServiceName(event.service_name),
          "service_name must be 1-64 alphanumeric, '_', '.', or '-' chars");

  require(event.outcome == "success" || event.outcome == "failure",
          "outcome must be 'success' or 'failure'");

  if (event.id.empty()) {
    event.id = util::uuidV4();
  } else {
    require(!event.id.empty() && event.id.size() <= 64 &&
                std::all_of(event.id.begin(), event.id.end(),
                            [](char raw) {
                              const auto c = static_cast<unsigned char>(raw);
                              return std::isalnum(c) != 0 || c == '_' ||
                                     c == '-';
                            }),
            "event_id must be 1-64 alphanumeric, '_' or '-' chars");
  }

  if (event.actor_id.has_value()) {
    require(isFreeIdentifier(*event.actor_id, kMaxIdentifier),
            "actor_id must be 1-128 printable chars");
  }
  if (event.target_id.has_value()) {
    require(isFreeIdentifier(*event.target_id, kMaxIdentifier),
            "target_id must be 1-128 printable chars");
  }

  if (event.occurred_at.empty()) {
    event.occurred_at = util::nowIso8601();
  } else {
    require(looksLikeIso8601(event.occurred_at),
            "occurred_at must be an ISO8601 timestamp");
  }

  if (event.metadata_json.empty())
    event.metadata_json = "{}";
  std::string error;
  const auto canonical = json::canonicalizeObject(event.metadata_json, error);
  if (!canonical.has_value()) {
    throw validationError("metadata must be a JSON object: " + error);
  }
  require(canonical->size() <= kMaxMetadataBytes,
          "metadata must not exceed 16 KiB after canonicalisation");
  event.metadata_json = *canonical;
}

AppendResult AuditService::append(domain::AuditEvent draft,
                                  std::string_view caller) {
  normalize(draft);
  policy_.authorizeWriter(caller, draft.service_name);

  for (int attempt = 0; attempt < kMaxAppendAttempts; ++attempt) {
    const auto head = repository_.latest();
    draft.seq = head.has_value() ? head->seq + 1 : 1;
    draft.prev_hash =
        head.has_value() ? head->hash : std::string(crypto::kGenesisHash);
    draft.recorded_at = util::nowIso8601();
    draft.hash = crypto::computeHash(draft);

    try {
      repository_.insert(draft);
      return AppendResult{draft, true};
    } catch (const repositories::DuplicateKeyError &) {
      // Re-sending a caller-supplied event_id is a retry, not a new event.
      if (const auto existing = repository_.findById(draft.id)) {
        return AppendResult{*existing, false};
      }
      // Otherwise another writer extended the same head: reseal and retry.
    }
  }

  throw conflictError(
      "could not append the audit event after repeated chain contention");
}

std::optional<domain::AuditEvent>
AuditService::get(const std::string &id, std::string_view caller) const {
  policy_.authorizeReader(caller);
  auto event = repository_.findById(id);
  if (!event.has_value())
    throw notFoundError("no audit event with this id");
  return event;
}

std::vector<domain::AuditEvent>
AuditService::query(domain::AuditQuery query, std::string_view caller) const {
  policy_.authorizeReader(caller);

  if (query.before_seq.has_value() && query.after_seq.has_value()) {
    throw validationError("before_seq and after_seq are mutually exclusive");
  }
  if (query.before_seq.has_value() && *query.before_seq < 0) {
    throw validationError("before_seq must be >= 0");
  }
  if (query.after_seq.has_value() && *query.after_seq < 0) {
    throw validationError("after_seq must be >= 0");
  }
  if (query.limit <= 0)
    query.limit = kDefaultQueryLimit;
  if (query.limit > kMaxQueryLimit)
    query.limit = kMaxQueryLimit;

  return repository_.find(query);
}

domain::ChainVerification
AuditService::verifyChain(std::string_view caller) const {
  policy_.authorizeReader(caller);

  domain::ChainVerification result;
  std::string expected_prev(crypto::kGenesisHash);
  int64_t expected_seq = 1;

  repository_.forEachOrdered([&](const domain::AuditEvent &event) {
    if (event.seq != expected_seq) {
      result.valid = false;
      result.first_broken_seq = event.seq;
      result.reason = "missing, duplicated or reordered event (expected seq " +
                      std::to_string(expected_seq) + ")";
      return false;
    }
    if (event.prev_hash != expected_prev) {
      result.valid = false;
      result.first_broken_seq = event.seq;
      result.reason = "prev_hash does not link to the previous event";
      return false;
    }
    if (crypto::computeHash(event) != event.hash) {
      result.valid = false;
      result.first_broken_seq = event.seq;
      result.reason = "stored hash does not match the event content";
      return false;
    }
    expected_prev = event.hash;
    expected_seq += 1;
    result.checked += 1;
    return true;
  });

  return result;
}

int64_t AuditService::count() const { return repository_.count(); }

bool AuditService::healthy() const { return repository_.healthy(); }

} // namespace audit::services
