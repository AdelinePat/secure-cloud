#include "audit/repositories/MongoAuditRepository.hpp"

#include <bson/bson.h>
#include <mongoc/mongoc.h>

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "audit/Errors.hpp"
#include "audit/json/Json.hpp"

namespace audit::repositories {
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
using CursorPtr = std::unique_ptr<mongoc_cursor_t, decltype(&mongoc_cursor_destroy)>;
using CollectionPtr =
    std::unique_ptr<mongoc_collection_t, decltype(&mongoc_collection_destroy)>;

BsonPtr makeBson() { return BsonPtr(bson_new()); }

std::string describe(const char* operation, const bson_error_t& error) {
  std::string message = operation;
  message += ": ";
  message += error.message[0] != '\0' ? error.message : "unknown MongoDB error";
  if (error.code != 0) {
    message += " (code " + std::to_string(error.code) + ")";
  }
  return message;
}

bool isDuplicateKey(const bson_error_t& error) {
  return error.code == 11000 ||
         std::string(error.message).find("E11000") != std::string::npos;
}

void appendStringOrNull(bson_t* document, const char* key,
                        const std::optional<std::string>& value) {
  if (value.has_value()) {
    BSON_APPEND_UTF8(document, key, value->c_str());
  } else {
    BSON_APPEND_NULL(document, key);
  }
}

std::string requiredString(const bson_t& document, const char* key) {
  bson_iter_t iter;
  if (!bson_iter_init_find(&iter, &document, key) || !BSON_ITER_HOLDS_UTF8(&iter)) {
    throw storageError(std::string("audit document is missing string field '") +
                       key + "'");
  }
  uint32_t length = 0;
  const char* value = bson_iter_utf8(&iter, &length);
  return std::string(value, length);
}

int64_t requiredInt64(const bson_t& document, const char* key) {
  bson_iter_t iter;
  if (!bson_iter_init_find(&iter, &document, key) ||
      !(BSON_ITER_HOLDS_INT32(&iter) || BSON_ITER_HOLDS_INT64(&iter))) {
    throw storageError(std::string("audit document is missing numeric field '") +
                       key + "'");
  }
  return bson_iter_as_int64(&iter);
}

std::optional<std::string> optionalString(const bson_t& document,
                                          const char* key) {
  bson_iter_t iter;
  if (!bson_iter_init_find(&iter, &document, key)) return std::nullopt;
  if (BSON_ITER_HOLDS_NULL(&iter)) return std::nullopt;
  if (!BSON_ITER_HOLDS_UTF8(&iter)) {
    throw storageError(std::string("audit document field '") + key +
                       "' is not a string");
  }
  uint32_t length = 0;
  const char* value = bson_iter_utf8(&iter, &length);
  return std::string(value, length);
}

std::string metadataOf(const bson_t& document) {
  bson_iter_t iter;
  if (!bson_iter_init_find(&iter, &document, "metadata")) return "{}";
  if (BSON_ITER_HOLDS_NULL(&iter)) return "{}";
  if (!BSON_ITER_HOLDS_DOCUMENT(&iter)) {
    throw storageError("audit document field 'metadata' is not a document");
  }

  const uint8_t* buffer = nullptr;
  uint32_t length = 0;
  bson_iter_document(&iter, &length, &buffer);

  bson_t metadata;
  if (!bson_init_static(&metadata, buffer, length)) {
    throw storageError("audit document contains malformed metadata");
  }
  const std::string canonical = json::canonicalizeDocument(metadata);
  bson_destroy(&metadata);
  return canonical;
}

BsonPtr toBson(const domain::AuditEvent& event) {
  BsonPtr document = makeBson();
  if (!document) throw storageError("out of memory while building a BSON document");

  BSON_APPEND_UTF8(document.get(), "_id", event.id.c_str());
  BSON_APPEND_INT64(document.get(), "seq", event.seq);
  BSON_APPEND_UTF8(document.get(), "event_type", event.event_type.c_str());
  BSON_APPEND_UTF8(document.get(), "service_name", event.service_name.c_str());
  appendStringOrNull(document.get(), "actor_id", event.actor_id);
  appendStringOrNull(document.get(), "target_id", event.target_id);
  BSON_APPEND_UTF8(document.get(), "outcome", event.outcome.c_str());
  BSON_APPEND_UTF8(document.get(), "occurred_at", event.occurred_at.c_str());
  BSON_APPEND_UTF8(document.get(), "recorded_at", event.recorded_at.c_str());
  BSON_APPEND_UTF8(document.get(), "prev_hash", event.prev_hash.c_str());
  BSON_APPEND_UTF8(document.get(), "hash", event.hash.c_str());

  bson_error_t error{};
  bson_t* metadata = bson_new_from_json(
      reinterpret_cast<const uint8_t*>(event.metadata_json.data()),
      static_cast<ssize_t>(event.metadata_json.size()), &error);
  if (metadata == nullptr) {
    throw storageError(describe("invalid canonical metadata", error));
  }
  const bool appended =
      bson_append_document(document.get(), "metadata", -1, metadata);
  bson_destroy(metadata);
  if (!appended) throw storageError("could not attach metadata to the document");

  return document;
}

domain::AuditEvent fromBson(const bson_t& document) {
  domain::AuditEvent event;
  event.id = requiredString(document, "_id");
  event.seq = requiredInt64(document, "seq");
  event.event_type = requiredString(document, "event_type");
  event.service_name = requiredString(document, "service_name");
  event.actor_id = optionalString(document, "actor_id");
  event.target_id = optionalString(document, "target_id");
  event.outcome = requiredString(document, "outcome");
  event.occurred_at = requiredString(document, "occurred_at");
  event.recorded_at = requiredString(document, "recorded_at");
  event.metadata_json = metadataOf(document);
  event.prev_hash = requiredString(document, "prev_hash");
  event.hash = requiredString(document, "hash");
  return event;
}

void appendSort(bson_t* options, bool ascending) {
  bson_t sort;
  bson_init(&sort);
  BSON_APPEND_INT32(&sort, "seq", ascending ? 1 : -1);
  BSON_APPEND_DOCUMENT(options, "sort", &sort);
  bson_destroy(&sort);
}

}  // namespace

struct MongoAuditRepository::Impl {
  mongoc_client_t* client = nullptr;
  std::string database;
  std::string collection;
};

MongoAuditRepository::MongoAuditRepository(MongoConfig config)
    : impl_(std::make_unique<Impl>()) {
  // libmongoc requires a process-wide initialisation performed exactly once.
  static std::once_flag init_flag;
  std::call_once(init_flag, [] { mongoc_init(); });

  impl_->database = config.database.empty() ? "auditDB" : config.database;
  impl_->collection = config.collection.empty() ? "auditLogs" : config.collection;

  bson_error_t error{};
  mongoc_uri_t* uri = nullptr;

  if (!config.uri.empty()) {
    uri = mongoc_uri_new_with_error(config.uri.c_str(), &error);
    if (uri == nullptr) {
      throw storageError(describe("invalid MONGO_URI", error));
    }
    if (const char* database = mongoc_uri_get_database(uri)) {
      impl_->database = database;
    }
  } else {
    uri = mongoc_uri_new_for_host_port(config.host.c_str(),
                                       static_cast<uint16_t>(config.port));
    if (uri == nullptr) {
      throw storageError("invalid MongoDB host/port");
    }
    mongoc_uri_set_database(uri, impl_->database.c_str());
    if (!config.username.empty()) {
      mongoc_uri_set_username(uri, config.username.c_str());
      mongoc_uri_set_password(uri, config.password.c_str());
      const std::string auth_source =
          config.auth_source.empty() ? impl_->database : config.auth_source;
      mongoc_uri_set_auth_source(uri, auth_source.c_str());
    }
  }

  impl_->client = mongoc_client_new_from_uri(uri);
  mongoc_uri_destroy(uri);
  if (impl_->client == nullptr) {
    throw storageError("could not create the MongoDB client");
  }
  mongoc_client_set_appname(impl_->client, "audit-service");
}

MongoAuditRepository::~MongoAuditRepository() {
  if (impl_ && impl_->client != nullptr) {
    mongoc_client_destroy(impl_->client);
  }
}

void MongoAuditRepository::ensureIndexes() {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  const auto create = [&](const bson_t* keys, const char* name, bool unique) {
    bson_t options;
    bson_init(&options);
    BSON_APPEND_UTF8(&options, "name", name);
    if (unique) BSON_APPEND_BOOL(&options, "unique", true);

    mongoc_index_model_t* model = mongoc_index_model_new(keys, &options);
    mongoc_index_model_t* models[] = {model};
    bson_error_t error{};
    const bool created = mongoc_collection_create_indexes_with_opts(
        collection.get(), models, 1, nullptr, nullptr, &error);
    mongoc_index_model_destroy(model);
    bson_destroy(&options);
    if (!created) {
      throw storageError(describe("could not create an audit index", error));
    }
  };

  bson_t keys;
  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "seq", 1);
  create(&keys, "uniq_seq", true);
  bson_destroy(&keys);

  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "prev_hash", 1);
  create(&keys, "uniq_prev_hash", true);
  bson_destroy(&keys);

  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "hash", 1);
  create(&keys, "uniq_hash", true);
  bson_destroy(&keys);

  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "event_type", 1);
  BSON_APPEND_INT32(&keys, "occurred_at", -1);
  create(&keys, "idx_event_type_occurred_at", false);
  bson_destroy(&keys);

  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "service_name", 1);
  BSON_APPEND_INT32(&keys, "occurred_at", -1);
  create(&keys, "idx_service_occurred_at", false);
  bson_destroy(&keys);

  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "actor_id", 1);
  create(&keys, "idx_actor_id", false);
  bson_destroy(&keys);

  bson_init(&keys);
  BSON_APPEND_INT32(&keys, "target_id", 1);
  create(&keys, "idx_target_id", false);
  bson_destroy(&keys);
}

void MongoAuditRepository::insert(const domain::AuditEvent& event) {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  const BsonPtr document = toBson(event);
  bson_error_t error{};
  if (!mongoc_collection_insert_one(collection.get(), document.get(), nullptr,
                                    nullptr, &error)) {
    if (isDuplicateKey(error)) throw DuplicateKeyError();
    throw storageError(describe("could not append an audit event", error));
  }
}

std::optional<domain::AuditEvent> MongoAuditRepository::findById(
    const std::string& id) {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  BsonPtr filter = makeBson();
  BSON_APPEND_UTF8(filter.get(), "_id", id.c_str());

  CursorPtr cursor(
      mongoc_collection_find_with_opts(collection.get(), filter.get(), nullptr,
                                       nullptr),
      &mongoc_cursor_destroy);
  if (!cursor) throw storageError("could not query the audit collection");

  const bson_t* document = nullptr;
  std::optional<domain::AuditEvent> result;
  while (mongoc_cursor_next(cursor.get(), &document)) {
    result = fromBson(*document);
    break;
  }

  bson_error_t error{};
  if (mongoc_cursor_error(cursor.get(), &error)) {
    throw storageError(describe("could not read an audit event", error));
  }
  return result;
}

std::optional<domain::AuditEvent> MongoAuditRepository::latest() {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  BsonPtr filter = makeBson();
  BsonPtr options = makeBson();
  appendSort(options.get(), false);
  BSON_APPEND_INT64(options.get(), "limit", 1);

  CursorPtr cursor(mongoc_collection_find_with_opts(
                       collection.get(), filter.get(), options.get(), nullptr),
                   &mongoc_cursor_destroy);
  if (!cursor) throw storageError("could not query the audit collection");

  const bson_t* document = nullptr;
  std::optional<domain::AuditEvent> result;
  while (mongoc_cursor_next(cursor.get(), &document)) {
    result = fromBson(*document);
    break;
  }

  bson_error_t error{};
  if (mongoc_cursor_error(cursor.get(), &error)) {
    throw storageError(describe("could not read the audit chain head", error));
  }
  return result;
}

std::vector<domain::AuditEvent> MongoAuditRepository::find(
    const domain::AuditQuery& query) {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  BsonPtr filter = makeBson();
  const auto appendEquals = [&](const char* key,
                                const std::optional<std::string>& value) {
    if (value.has_value()) {
      BSON_APPEND_UTF8(filter.get(), key, value->c_str());
    }
  };
  appendEquals("event_type", query.event_type);
  appendEquals("service_name", query.service_name);
  appendEquals("actor_id", query.actor_id);
  appendEquals("target_id", query.target_id);
  appendEquals("outcome", query.outcome);

  if (query.from.has_value() || query.to.has_value()) {
    bson_t occurred_at;
    BSON_APPEND_DOCUMENT_BEGIN(filter.get(), "occurred_at", &occurred_at);
    if (query.from.has_value()) {
      BSON_APPEND_UTF8(&occurred_at, "$gte", query.from->c_str());
    }
    if (query.to.has_value()) {
      BSON_APPEND_UTF8(&occurred_at, "$lte", query.to->c_str());
    }
    bson_append_document_end(filter.get(), &occurred_at);
  }

  if (query.before_seq.has_value() || query.after_seq.has_value()) {
    bson_t seq;
    BSON_APPEND_DOCUMENT_BEGIN(filter.get(), "seq", &seq);
    if (query.before_seq.has_value()) {
      BSON_APPEND_INT64(&seq, "$lt", *query.before_seq);
    } else {
      BSON_APPEND_INT64(&seq, "$gt", *query.after_seq);
    }
    bson_append_document_end(filter.get(), &seq);
  }

  BsonPtr options = makeBson();
  appendSort(options.get(), query.ascending);
  BSON_APPEND_INT64(options.get(), "limit", query.limit);

  CursorPtr cursor(mongoc_collection_find_with_opts(
                       collection.get(), filter.get(), options.get(), nullptr),
                   &mongoc_cursor_destroy);
  if (!cursor) throw storageError("could not query the audit collection");

  std::vector<domain::AuditEvent> events;
  const bson_t* document = nullptr;
  while (mongoc_cursor_next(cursor.get(), &document)) {
    events.push_back(fromBson(*document));
  }

  bson_error_t error{};
  if (mongoc_cursor_error(cursor.get(), &error)) {
    throw storageError(describe("could not read audit events", error));
  }
  return events;
}

void MongoAuditRepository::forEachOrdered(
    const std::function<bool(const domain::AuditEvent&)>& visitor) {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  BsonPtr filter = makeBson();
  BsonPtr options = makeBson();
  appendSort(options.get(), true);

  CursorPtr cursor(mongoc_collection_find_with_opts(
                       collection.get(), filter.get(), options.get(), nullptr),
                   &mongoc_cursor_destroy);
  if (!cursor) throw storageError("could not query the audit collection");

  const bson_t* document = nullptr;
  bool keep_going = true;
  while (keep_going && mongoc_cursor_next(cursor.get(), &document)) {
    keep_going = visitor(fromBson(*document));
  }

  bson_error_t error{};
  if (mongoc_cursor_error(cursor.get(), &error)) {
    throw storageError(describe("could not walk the audit chain", error));
  }
}

int64_t MongoAuditRepository::count() {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) throw storageError("could not get the audit collection");

  BsonPtr filter = makeBson();
  BsonPtr reply = makeBson();
  bson_error_t error{};
  const int64_t total = mongoc_collection_count_documents(
      collection.get(), filter.get(), nullptr, nullptr, reply.get(), &error);
  if (total < 0) {
    throw storageError(describe("could not count audit events", error));
  }
  return total;
}

bool MongoAuditRepository::healthy() {
  CollectionPtr collection(mongoc_client_get_collection(
                               impl_->client, impl_->database.c_str(),
                               impl_->collection.c_str()),
                           &mongoc_collection_destroy);
  if (!collection) return false;

  BsonPtr command = makeBson();
  BSON_APPEND_INT32(command.get(), "ping", 1);
  BsonPtr reply = makeBson();
  bson_error_t error{};
  return mongoc_collection_command_simple(collection.get(), command.get(),
                                          nullptr, reply.get(), &error);
}

}  // namespace audit::repositories
