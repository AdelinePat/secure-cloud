#include "audit/http/AuditHttpServer.hpp"

#include <bson/bson.h>
#include <httplib.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "audit/Errors.hpp"
#include "audit/domain/AuditEvent.hpp"
#include "audit/json/Json.hpp"
#include "audit/util/Uuid.hpp"

namespace audit::http {
namespace {

struct BsonDeleter {
  void operator()(bson_t* document) const {
    if (document != nullptr) bson_destroy(document);
  }
};

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#endif
using BsonPtr = std::unique_ptr<bson_t, BsonDeleter>;
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

constexpr std::size_t kMaxBatchEvents = 500;

std::string requestId(const httplib::Request& request) {
  const std::string provided = request.get_header_value("X-Request-Id");
  const bool safe =
      !provided.empty() && provided.size() <= 128 &&
      std::all_of(provided.begin(), provided.end(), [](unsigned char c) {
        return c >= 0x20 && c != 0x7F;
      });
  return safe ? provided : util::uuidV4();
}

void sendJson(httplib::Response& response, int status, const std::string& body,
              const std::string& id) {
  response.status = status;
  response.set_header("X-Request-Id", id);
  response.set_content(body, "application/json");
}

std::string errorBody(int code, const std::string& type,
                      const std::string& message, const std::string& id) {
  return "{\"code\":" + std::to_string(code) + ",\"type\":" + json::quote(type) +
         ",\"message\":" + json::quote(message) +
         ",\"request_id\":" + json::quote(id) + "}";
}

int statusFor(ErrorKind kind) {
  switch (kind) {
    case ErrorKind::Validation:
      return 400;
    case ErrorKind::Forbidden:
      return 403;
    case ErrorKind::NotFound:
      return 404;
    case ErrorKind::Conflict:
      return 409;
    case ErrorKind::Storage:
      return 503;
  }
  return 500;
}

std::string typeFor(ErrorKind kind) {
  switch (kind) {
    case ErrorKind::Validation:
      return "VALIDATION_ERROR";
    case ErrorKind::Forbidden:
      return "FORBIDDEN";
    case ErrorKind::NotFound:
      return "NOT_FOUND";
    case ErrorKind::Conflict:
      return "CONFLICT";
    case ErrorKind::Storage:
      return "STORAGE_UNAVAILABLE";
  }
  return "INTERNAL_ERROR";
}

std::string eventJson(const domain::AuditEvent& event) {
  std::string out = "{";
  out += "\"id\":" + json::quote(event.id);
  out += ",\"seq\":" + std::to_string(event.seq);
  out += ",\"event_type\":" + json::quote(event.event_type);
  out += ",\"service_name\":" + json::quote(event.service_name);
  out += ",\"actor_id\":";
  out += event.actor_id.has_value() ? json::quote(*event.actor_id) : "null";
  out += ",\"target_id\":";
  out += event.target_id.has_value() ? json::quote(*event.target_id) : "null";
  out += ",\"outcome\":" + json::quote(event.outcome);
  out += ",\"occurred_at\":" + json::quote(event.occurred_at);
  out += ",\"recorded_at\":" + json::quote(event.recorded_at);
  out += ",\"metadata\":";
  out += event.metadata_json.empty() ? "{}" : event.metadata_json;
  out += ",\"prev_hash\":" + json::quote(event.prev_hash);
  out += ",\"hash\":" + json::quote(event.hash);
  out += '}';
  return out;
}

std::string verificationJson(const domain::ChainVerification& result) {
  std::string out = "{";
  out += "\"valid\":";
  out += result.valid ? "true" : "false";
  out += ",\"checked\":" + std::to_string(result.checked);
  out += ",\"first_broken_seq\":";
  out += result.first_broken_seq.has_value()
             ? std::to_string(*result.first_broken_seq)
             : "null";
  out += ",\"reason\":" + json::quote(result.reason);
  out += '}';
  return out;
}

std::optional<std::string> optionalStringField(const bson_t& document,
                                               const char* key) {
  bson_iter_t iter;
  if (!bson_iter_init_find(&iter, &document, key) || BSON_ITER_HOLDS_NULL(&iter)) {
    return std::nullopt;
  }
  if (!BSON_ITER_HOLDS_UTF8(&iter)) {
    throw validationError(std::string(key) + " must be a string");
  }
  uint32_t length = 0;
  const char* value = bson_iter_utf8(&iter, &length);
  return std::string(value, length);
}

std::string requiredStringField(const bson_t& document, const char* key) {
  auto value = optionalStringField(document, key);
  if (!value.has_value() || value->empty()) {
    throw validationError(std::string(key) + " is required and must be a string");
  }
  return *value;
}

BsonPtr parseObject(const std::string& body, std::string& error) {
  bson_error_t bson_error{};
  bson_t* document = bson_new_from_json(
      reinterpret_cast<const uint8_t*>(body.data()),
      static_cast<ssize_t>(body.size()), &bson_error);
  if (document == nullptr) {
    error = bson_error.message[0] != '\0' ? bson_error.message
                                          : "body must be a JSON object";
    return BsonPtr(nullptr, BsonDeleter{});
  }
  return BsonPtr(document);
}

domain::AuditEvent draftFromBson(const bson_t& document) {
  domain::AuditEvent draft;
  draft.id = optionalStringField(document, "event_id").value_or("");
  draft.event_type = requiredStringField(document, "event_type");
  draft.service_name = requiredStringField(document, "service_name");
  draft.actor_id = optionalStringField(document, "actor_id");
  draft.target_id = optionalStringField(document, "target_id");
  draft.outcome = requiredStringField(document, "outcome");
  draft.occurred_at = optionalStringField(document, "occurred_at").value_or("");

  bson_iter_t metadata;
  if (bson_iter_init_find(&metadata, &document, "metadata") &&
      !BSON_ITER_HOLDS_NULL(&metadata)) {
    if (!BSON_ITER_HOLDS_DOCUMENT(&metadata)) {
      throw validationError("metadata must be a JSON object");
    }
    const uint8_t* buffer = nullptr;
    uint32_t length = 0;
    bson_iter_document(&metadata, &length, &buffer);
    bson_t nested;
    if (!bson_init_static(&nested, buffer, length)) {
      throw validationError("metadata must be a JSON object");
    }
    draft.metadata_json = json::canonicalizeDocument(nested);
    bson_destroy(&nested);
  }

  return draft;
}

std::string callerOf(const httplib::Request& request, bool tls_enabled) {
#ifdef CPPHTTPLIB_SSL_ENABLED
  if (tls_enabled) return request.peer_cert().subject_cn();
#else
  (void)request;
  (void)tls_enabled;
#endif
  return "";
}

int64_t parseSeq(const std::string& raw, const char* name) {
  int64_t value = 0;
  const auto result =
      std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (result.ec != std::errc() || result.ptr != raw.data() + raw.size() ||
      value < 0) {
    throw validationError(std::string(name) + " must be a non-negative integer");
  }
  return value;
}

domain::AuditQuery queryFromRequest(const httplib::Request& request) {
  domain::AuditQuery query;

  const auto param = [&](const char* name) -> std::optional<std::string> {
    if (!request.has_param(name)) return std::nullopt;
    std::string value = request.get_param_value(name);
    if (value.empty()) return std::nullopt;
    return value;
  };

  query.event_type = param("event_type");
  query.service_name = param("service_name");
  query.actor_id = param("actor_id");
  query.target_id = param("target_id");
  query.outcome = param("outcome");
  query.from = param("from");
  query.to = param("to");

  if (const auto limit = param("limit")) {
    query.limit = static_cast<int>(parseSeq(*limit, "limit"));
  }
  if (const auto before = param("before_seq")) {
    query.before_seq = parseSeq(*before, "before_seq");
  }
  if (const auto after = param("after_seq")) {
    query.after_seq = parseSeq(*after, "after_seq");
  }
  if (const auto order = param("order")) {
    if (*order == "asc") {
      query.ascending = true;
    } else if (*order == "desc") {
      query.ascending = false;
    } else {
      throw validationError("order must be 'asc' or 'desc'");
    }
  }
  return query;
}

std::vector<domain::AuditEvent> draftsFromBatch(const bson_t& document) {
  bson_iter_t events;
  if (!bson_iter_init_find(&events, &document, "events") ||
      !BSON_ITER_HOLDS_ARRAY(&events)) {
    throw validationError("events must be a JSON array");
  }

  const uint8_t* buffer = nullptr;
  uint32_t length = 0;
  bson_iter_array(&events, &length, &buffer);

  bson_t array;
  if (!bson_init_static(&array, buffer, length)) {
    throw validationError("events must be a JSON array");
  }

  std::vector<domain::AuditEvent> drafts;
  bson_iter_t element;
  bson_iter_init(&element, &array);
  while (bson_iter_next(&element)) {
    if (!BSON_ITER_HOLDS_DOCUMENT(&element)) {
      throw validationError("each entry of events must be a JSON object");
    }
    if (drafts.size() >= kMaxBatchEvents) {
      throw validationError("a batch may contain at most " +
                            std::to_string(kMaxBatchEvents) + " events");
    }
    const uint8_t* event_buffer = nullptr;
    uint32_t event_length = 0;
    bson_iter_document(&element, &event_length, &event_buffer);
    bson_t event;
    if (!bson_init_static(&event, event_buffer, event_length)) {
      throw validationError("malformed event in batch");
    }
    drafts.push_back(draftFromBson(event));
    bson_destroy(&event);
  }
  bson_destroy(&array);
  return drafts;
}

}  // namespace

struct AuditHttpServer::Impl {
  std::unique_ptr<httplib::Server> server;
};

AuditHttpServer::AuditHttpServer(HttpConfig config,
                                 services::AuditService& service)
    : impl_(std::make_unique<Impl>()),
      config_(std::move(config)),
      service_(service) {
  if (config_.tls_enabled) {
#ifdef CPPHTTPLIB_SSL_ENABLED
    auto server = std::make_unique<httplib::SSLServer>(
        config_.cert_path.c_str(), config_.key_path.c_str(),
        config_.ca_path.c_str());
    if (!server->is_valid()) {
      throw std::runtime_error(
          "invalid TLS configuration (certificate, key or CA could not be loaded)");
    }
    impl_->server = std::move(server);
#else
    throw std::runtime_error(
        "AUDIT_TLS_ENABLED is set but cpp-httplib was built without OpenSSL");
#endif
  } else {
    impl_->server = std::make_unique<httplib::Server>();
  }

  impl_->server->set_payload_max_length(config_.max_payload_bytes);
  impl_->server->set_read_timeout(10, 0);
  impl_->server->set_write_timeout(10, 0);
  impl_->server->set_keep_alive_max_count(64);

  impl_->server->set_logger([](const httplib::Request& request,
                               const httplib::Response& response) {
    std::cerr << "[audit] " << request.method << ' ' << request.path << " -> "
              << response.status << " request_id="
              << response.get_header_value("X-Request-Id") << '\n';
  });

  impl_->server->set_error_handler(
      [](const httplib::Request& request, httplib::Response& response) {
        if (!response.body.empty()) return;
        const std::string id = requestId(request);
        const int status = response.status >= 400 ? response.status : 404;
        const std::string type = status == 404 ? "NOT_FOUND"
                                 : (status == 400 ? "VALIDATION_ERROR" : "ERROR");
        response.status = status;
        sendJson(response, status,
                 errorBody(status, type,
                           status == 404 ? "no such route" : "request rejected",
                           id),
                 id);
      });

  impl_->server->Post("/audit/events",
                      [this](const httplib::Request& request,
                             httplib::Response& response) {
                        const std::string id = requestId(request);
                        try {
                          std::string error;
                          const BsonPtr document = parseObject(request.body, error);
                          if (!document) {
                            throw validationError("invalid JSON body: " + error);
                          }
                          const auto result = service_.append(
                              draftFromBson(*document), callerOf(request, config_.tls_enabled));
                          sendJson(response, result.created ? 201 : 200,
                                   eventJson(result.event), id);
                        } catch (const AuditError& exception) {
                          sendJson(response, statusFor(exception.kind()),
                                   errorBody(statusFor(exception.kind()),
                                             typeFor(exception.kind()),
                                             exception.what(), id),
                                   id);
                        } catch (const std::exception& exception) {
                          std::cerr << "[audit] unhandled error request_id=" << id
                                    << " " << exception.what() << '\n';
                          sendJson(response, 500,
                                   errorBody(500, "INTERNAL_ERROR",
                                             "internal server error", id),
                                   id);
                        }
                      });

  impl_->server->Post("/audit/events/batch",
                      [this](const httplib::Request& request,
                             httplib::Response& response) {
                        const std::string id = requestId(request);
                        std::string events_json;
                        std::size_t appended = 0;
                        try {
                          std::string error;
                          const BsonPtr document = parseObject(request.body, error);
                          if (!document) {
                            throw validationError("invalid JSON body: " + error);
                          }
                          const auto drafts = draftsFromBatch(*document);
                          bool first = true;
                          for (const auto& draft : drafts) {
                            const auto result = service_.append(
                                draft, callerOf(request, config_.tls_enabled));
                            if (!first) events_json += ',';
                            first = false;
                            events_json += eventJson(result.event);
                            ++appended;
                          }
                          sendJson(response, 201,
                                   "{\"appended\":" + std::to_string(appended) +
                                       ",\"events\":[" + events_json + "]}",
                                   id);
                        } catch (const AuditError& exception) {
                          const int status = statusFor(exception.kind());
                          const std::string body =
                              "{\"code\":" + std::to_string(status) +
                              ",\"type\":" + json::quote(typeFor(exception.kind())) +
                              ",\"message\":" + json::quote(exception.what()) +
                              ",\"request_id\":" + json::quote(id) +
                              ",\"appended\":" + std::to_string(appended) + "}";
                          sendJson(response, status, body, id);
                        } catch (const std::exception& exception) {
                          std::cerr << "[audit] unhandled error request_id=" << id
                                    << " " << exception.what() << '\n';
                          const std::string body =
                              "{\"code\":500,\"type\":\"INTERNAL_ERROR\","
                              "\"message\":\"internal server error\","
                              "\"request_id\":" + json::quote(id) +
                              ",\"appended\":" + std::to_string(appended) + "}";
                          sendJson(response, 500, body, id);
                        }
                      });

  impl_->server->Get("/audit/events",
                     [this](const httplib::Request& request,
                            httplib::Response& response) {
                       const std::string id = requestId(request);
                       try {
                         const auto events = service_.query(
                             queryFromRequest(request),
                             callerOf(request, config_.tls_enabled));
                         std::string body = "{\"events\":[";
                         bool first = true;
                         for (const auto& event : events) {
                           if (!first) body += ',';
                           first = false;
                           body += eventJson(event);
                         }
                         body += "],\"count\":" + std::to_string(events.size()) +
                                 '}';
                         sendJson(response, 200, body, id);
                       } catch (const AuditError& exception) {
                         sendJson(response, statusFor(exception.kind()),
                                  errorBody(statusFor(exception.kind()),
                                            typeFor(exception.kind()),
                                            exception.what(), id),
                                  id);
                       } catch (const std::exception& exception) {
                         std::cerr << "[audit] unhandled error request_id=" << id
                                   << " " << exception.what() << '\n';
                         sendJson(response, 500,
                                  errorBody(500, "INTERNAL_ERROR",
                                            "internal server error", id),
                                  id);
                       }
                     });

  impl_->server->Get(
      R"(/audit/events/([A-Za-z0-9_-]{1,64}))",
      [this](const httplib::Request& request, httplib::Response& response) {
        const std::string id = requestId(request);
        try {
          const std::string event_id = request.matches[1];
          const auto event =
              service_.get(event_id, callerOf(request, config_.tls_enabled));
          if (!event.has_value()) {
            throw notFoundError("no audit event with this id");
          }
          sendJson(response, 200, eventJson(*event), id);
        } catch (const AuditError& exception) {
          sendJson(response, statusFor(exception.kind()),
                   errorBody(statusFor(exception.kind()),
                             typeFor(exception.kind()), exception.what(), id),
                   id);
        } catch (const std::exception& exception) {
          std::cerr << "[audit] unhandled error request_id=" << id << " "
                    << exception.what() << '\n';
          sendJson(response, 500,
                   errorBody(500, "INTERNAL_ERROR", "internal server error", id),
                   id);
        }
      });

  impl_->server->Get("/audit/verify",
                     [this](const httplib::Request& request,
                            httplib::Response& response) {
                       const std::string id = requestId(request);
                       try {
                         const auto result = service_.verifyChain(
                             callerOf(request, config_.tls_enabled));
                         sendJson(response, 200, verificationJson(result), id);
                       } catch (const AuditError& exception) {
                         sendJson(response, statusFor(exception.kind()),
                                  errorBody(statusFor(exception.kind()),
                                            typeFor(exception.kind()),
                                            exception.what(), id),
                                  id);
                       } catch (const std::exception& exception) {
                         std::cerr << "[audit] unhandled error request_id=" << id
                                   << " " << exception.what() << '\n';
                         sendJson(response, 500,
                                  errorBody(500, "INTERNAL_ERROR",
                                            "internal server error", id),
                                  id);
                       }
                     });

  impl_->server->Get("/health",
                     [this](const httplib::Request& request,
                            httplib::Response& response) {
                       const std::string id = requestId(request);
                       if (service_.healthy()) {
                         sendJson(response, 200,
                                  "{\"status\":\"ok\",\"mongo\":\"up\"}", id);
                       } else {
                         sendJson(response, 503,
                                  "{\"status\":\"degraded\",\"mongo\":\"down\"}",
                                  id);
                       }
                     });
}

AuditHttpServer::~AuditHttpServer() = default;

bool AuditHttpServer::run() {
  return impl_->server->listen(config_.host, config_.port);
}

int AuditHttpServer::bindToAnyPort(const std::string& host) {
  return impl_->server->bind_to_any_port(host);
}

bool AuditHttpServer::listenAfterBind() {
  return impl_->server->listen_after_bind();
}

void AuditHttpServer::stop() { impl_->server->stop(); }

bool AuditHttpServer::is_valid() const { return impl_->server->is_valid(); }

}  // namespace audit::http
