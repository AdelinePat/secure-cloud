#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "audit/Errors.hpp"
#include "audit/crypto/HashChain.hpp"
#include "audit/services/AuditService.hpp"
#include "repositories/InMemoryAuditRepository.hpp"

namespace {

using audit::domain::AuditEvent;
using audit::services::ServicePolicy;

class AuditServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    policy_.allowed_services = {"auth", "file", "messaging", "gateway"};
    policy_.require_caller_identity = true;
    service_ = std::make_unique<audit::services::AuditService>(repository_,
                                                               policy_);
  }

  static AuditEvent draft(const std::string& event_type = "login_succeeded",
                          const std::string& service = "auth") {
    AuditEvent event;
    event.event_type = event_type;
    event.service_name = service;
    event.actor_id = "user-1";
    event.target_id = "target-1";
    event.outcome = "success";
    event.metadata_json = R"({"ip":"127.0.0.1"})";
    return event;
  }

  audit::tests::InMemoryAuditRepository repository_;
  ServicePolicy policy_;
  std::unique_ptr<audit::services::AuditService> service_;
};

TEST_F(AuditServiceTest, SealsTheFirstEventIntoTheGenesisBlock) {
  const auto result = service_->append(draft(), "auth");

  EXPECT_TRUE(result.created);
  EXPECT_EQ(result.event.id.size(), 36u);
  EXPECT_EQ(result.event.seq, 1);
  EXPECT_EQ(result.event.prev_hash, audit::crypto::kGenesisHash);
  EXPECT_EQ(result.event.hash.size(), 64u);
  EXPECT_EQ(result.event.hash, audit::crypto::computeHash(result.event));
  EXPECT_FALSE(result.event.recorded_at.empty());
  EXPECT_EQ(result.event.occurred_at, result.event.recorded_at);
}

TEST_F(AuditServiceTest, LinksEachEventToThePreviousHash) {
  const auto first = service_->append(draft(), "auth");
  const auto second = service_->append(draft("login_failed"), "auth");

  EXPECT_EQ(second.event.seq, 2);
  EXPECT_EQ(second.event.prev_hash, first.event.hash);
  EXPECT_NE(second.event.hash, first.event.hash);
}

TEST_F(AuditServiceTest, ReplaysCallerSuppliedEventIdIdempotently) {
  auto event = draft();
  event.id = "evt-001";

  const auto first = service_->append(event, "auth");
  const auto replay = service_->append(event, "auth");

  EXPECT_TRUE(first.created);
  EXPECT_FALSE(replay.created);
  EXPECT_EQ(replay.event.seq, first.event.seq);
  EXPECT_EQ(replay.event.hash, first.event.hash);
  EXPECT_EQ(repository_.count(), 1);
}

TEST_F(AuditServiceTest, RejectsServiceNameSpoofing) {
  EXPECT_THROW(service_->append(draft("file_uploaded", "file"), "auth"),
               audit::AuditError);
}

TEST_F(AuditServiceTest, RejectsServicesOutsideTheAllowList) {
  EXPECT_THROW(service_->append(draft("login_succeeded", "unknown"), "unknown"),
               audit::AuditError);
}

TEST_F(AuditServiceTest, RejectsMissingCallerIdentityWhenRequired) {
  EXPECT_THROW(service_->append(draft(), ""), audit::AuditError);
}

TEST_F(AuditServiceTest, AllowsMissingCallerInDevMode) {
  policy_.require_caller_identity = false;
  audit::services::AuditService dev_service(repository_, policy_);

  EXPECT_NO_THROW(dev_service.append(draft(), ""));
}

TEST_F(AuditServiceTest, ValidatesEventFields) {
  auto event = draft();
  event.outcome = "maybe";
  EXPECT_THROW(service_->append(event, "auth"), audit::AuditError);

  event = draft();
  event.event_type = "File Uploaded";
  EXPECT_THROW(service_->append(event, "auth"), audit::AuditError);

  event = draft();
  event.occurred_at = "yesterday";
  EXPECT_THROW(service_->append(event, "auth"), audit::AuditError);

  event = draft();
  event.metadata_json = "[1,2,3]";
  EXPECT_THROW(service_->append(event, "auth"), audit::AuditError);
}

TEST_F(AuditServiceTest, CanonicalisesMetadataBeforeHashing) {
  auto event = draft();
  event.metadata_json = R"({ "b": 1, "a": { "z": true, "y": [1, 2] } })";

  const auto result = service_->append(event, "auth");

  EXPECT_EQ(result.event.metadata_json, R"({"a":{"y":[1,2],"z":true},"b":1})");
  EXPECT_EQ(result.event.hash, audit::crypto::computeHash(result.event));
}

TEST_F(AuditServiceTest, QueryFiltersAndPaginates) {
  service_->append(draft("login_succeeded"), "auth");
  service_->append(draft("file_uploaded", "file"), "file");
  service_->append(draft("login_failed"), "auth");
  service_->append(draft("file_downloaded", "file"), "file");

  audit::domain::AuditQuery query;
  query.event_type = "login_succeeded";
  EXPECT_EQ(service_->query(query, "gateway").size(), 1u);

  query = {};
  query.service_name = "file";
  EXPECT_EQ(service_->query(query, "gateway").size(), 2u);

  query = {};
  query.limit = 2;
  EXPECT_EQ(service_->query(query, "gateway").size(), 2u);

  query = {};
  query.limit = 2;
  query.ascending = true;
  const auto page = service_->query(query, "gateway");
  ASSERT_EQ(page.size(), 2u);
  EXPECT_EQ(page[0].seq, 1);
  EXPECT_EQ(page[1].seq, 2);

  query = {};
  query.before_seq = 3;
  query.ascending = true;
  const auto before = service_->query(query, "gateway");
  ASSERT_EQ(before.size(), 2u);
  EXPECT_EQ(before.back().seq, 2);
}

TEST_F(AuditServiceTest, RejectsConflictingPaginationBounds) {
  audit::domain::AuditQuery query;
  query.before_seq = 10;
  query.after_seq = 2;
  EXPECT_THROW(service_->query(query, "gateway"), audit::AuditError);
}

TEST_F(AuditServiceTest, VerifiesAnIntactChain) {
  service_->append(draft(), "auth");
  service_->append(draft("file_uploaded", "file"), "file");
  service_->append(draft("login_failed"), "auth");

  const auto result = service_->verifyChain("gateway");
  EXPECT_TRUE(result.valid);
  EXPECT_EQ(result.checked, 3);
  EXPECT_FALSE(result.first_broken_seq.has_value());
}

TEST_F(AuditServiceTest, DetectsATamperedEvent) {
  service_->append(draft(), "auth");
  service_->append(draft("file_uploaded", "file"), "file");

  auto events = repository_.all();
  auto tampered = events[0];
  tampered.outcome = "failure";
  repository_.overwrite(tampered);

  const auto result = service_->verifyChain("gateway");
  EXPECT_FALSE(result.valid);
  ASSERT_TRUE(result.first_broken_seq.has_value());
  EXPECT_EQ(*result.first_broken_seq, 1);
  EXPECT_NE(result.reason.find("hash"), std::string::npos);
}

TEST_F(AuditServiceTest, DetectsADeletedEvent) {
  service_->append(draft(), "auth");
  service_->append(draft("file_uploaded", "file"), "file");
  service_->append(draft("login_failed"), "auth");

  repository_.erase(2);

  const auto result = service_->verifyChain("gateway");
  EXPECT_FALSE(result.valid);
  ASSERT_TRUE(result.first_broken_seq.has_value());
  EXPECT_EQ(*result.first_broken_seq, 3);
}

TEST_F(AuditServiceTest, ThrowsNotFoundForUnknownEvent) {
  EXPECT_THROW(service_->get("does-not-exist", "gateway"), audit::AuditError);
}

TEST_F(AuditServiceTest, ReadersMustBeAllowedServices) {
  service_->append(draft(), "auth");
  EXPECT_THROW(service_->query({}, "stranger"), audit::AuditError);
  EXPECT_NO_THROW(service_->query({}, "gateway"));
}

}  // namespace
