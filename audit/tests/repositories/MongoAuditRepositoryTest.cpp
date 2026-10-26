#include <gtest/gtest.h>
#include <mongoc/mongoc.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "audit/repositories/MongoAuditRepository.hpp"
#include "audit/services/AuditService.hpp"
#include "audit/util/Uuid.hpp"

// Integration test: only runs when AUDIT_TEST_MONGO_URI is set, e.g.
//   AUDIT_TEST_MONGO_URI='mongodb://auditServiceUser:pwd@localhost:27017/audit_test?authSource=audit_test'
//
// It creates a throwaway collection, exercises the real driver, and drops the
// collection afterwards so it can run against a shared dev database.
namespace {

using audit::domain::AuditEvent;
using audit::repositories::MongoAuditRepository;
using audit::repositories::MongoConfig;
using audit::services::ServicePolicy;

std::string databaseOf(const std::string& uri) {
  mongoc_uri_t* parsed = mongoc_uri_new(uri.c_str());
  if (parsed == nullptr) return "audit_test";
  const char* database = mongoc_uri_get_database(parsed);
  std::string result = database != nullptr ? database : "audit_test";
  mongoc_uri_destroy(parsed);
  return result;
}

class CollectionGuard {
 public:
  CollectionGuard(std::string uri, std::string database, std::string collection)
      : client_(mongoc_client_new(uri.c_str())),
        database_(std::move(database)),
        collection_(std::move(collection)) {}

  ~CollectionGuard() {
    if (client_ == nullptr) return;
    mongoc_collection_t* collection = mongoc_client_get_collection(
        client_, database_.c_str(), collection_.c_str());
    bson_error_t error{};
    mongoc_collection_drop(collection, &error);
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client_);
  }

  bool setField(const std::string& id, const std::string& field,
                const std::string& value) {
    if (client_ == nullptr) return false;
    mongoc_collection_t* collection = mongoc_client_get_collection(
        client_, database_.c_str(), collection_.c_str());

    bson_t filter;
    bson_init(&filter);
    BSON_APPEND_UTF8(&filter, "_id", id.c_str());

    bson_t update;
    bson_init(&update);
    bson_t set;
    BSON_APPEND_DOCUMENT_BEGIN(&update, "$set", &set);
    BSON_APPEND_UTF8(&set, field.c_str(), value.c_str());
    bson_append_document_end(&update, &set);

    bson_error_t error{};
    const bool ok = mongoc_collection_update_one(
        collection, &filter, &update, nullptr, nullptr, &error);

    bson_destroy(&update);
    bson_destroy(&filter);
    mongoc_collection_destroy(collection);
    return ok;
  }

 private:
  mongoc_client_t* client_ = nullptr;
  std::string database_;
  std::string collection_;
};

AuditEvent draft(const std::string& event_type, const std::string& service) {
  AuditEvent event;
  event.event_type = event_type;
  event.service_name = service;
  event.actor_id = "user-1";
  event.target_id = "target-1";
  event.outcome = "success";
  event.metadata_json = R"({"ip":"127.0.0.1"})";
  return event;
}

}  // namespace

TEST(MongoAuditRepositoryTest, AppendsQueriesAndDetectsTampering) {
  const char* uri = std::getenv("AUDIT_TEST_MONGO_URI");
  if (uri == nullptr || uri[0] == '\0') {
    GTEST_SKIP() << "set AUDIT_TEST_MONGO_URI to run the MongoDB integration test";
  }

  const std::string database = databaseOf(uri);
  const std::string collection = "auditLogs_test_" + audit::util::uuidV4();

  MongoConfig config;
  config.uri = uri;
  config.database = database;
  config.collection = collection;

  // The repository initialises libmongoc; create it before any raw client.
  MongoAuditRepository repository(config);
  CollectionGuard cleanup(uri, database, collection);
  repository.ensureIndexes();

  ServicePolicy policy;
  policy.allowed_services = {"auth", "file", "messaging", "gateway"};
  policy.require_caller_identity = false;
  audit::services::AuditService service(repository, policy);

  const auto first = service.append(draft("login_succeeded", "auth"), "");
  const auto second = service.append(draft("file_uploaded", "file"), "");
  const auto third = service.append(draft("file_downloaded", "file"), "");

  ASSERT_EQ(first.event.seq, 1);
  ASSERT_EQ(second.event.seq, 2);
  ASSERT_EQ(third.event.seq, 3);
  ASSERT_EQ(second.event.prev_hash, first.event.hash);
  ASSERT_EQ(repository.count(), 3);

  const auto by_id = repository.findById(second.event.id);
  ASSERT_TRUE(by_id.has_value());
  EXPECT_EQ(by_id->hash, second.event.hash);

  audit::domain::AuditQuery query;
  query.service_name = "file";
  query.ascending = true;
  const auto files = service.query(query, "gateway");
  ASSERT_EQ(files.size(), 2u);
  EXPECT_EQ(files[0].event_type, "file_uploaded");

  const auto verification = service.verifyChain("gateway");
  EXPECT_TRUE(verification.valid);
  EXPECT_EQ(verification.checked, 3);

  // Same event id twice must not create a second row.
  auto replay = draft("login_succeeded", "auth");
  replay.id = first.event.id;
  const auto replayed = service.append(replay, "");
  EXPECT_FALSE(replayed.created);
  EXPECT_EQ(repository.count(), 3);

  // Direct write bypassing the service: the chain must notice.
  ASSERT_TRUE(cleanup.setField(first.event.id, "outcome", "failure"));
  const auto tampered = service.verifyChain("gateway");
  EXPECT_FALSE(tampered.valid);
  EXPECT_EQ(tampered.first_broken_seq, 1);
}
