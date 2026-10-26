#include <gtest/gtest.h>
#include <httplib.h>

#include <memory>
#include <string>
#include <thread>

#include "audit/http/AuditHttpServer.hpp"
#include "audit/services/AuditService.hpp"
#include "repositories/InMemoryAuditRepository.hpp"

namespace {

using audit::services::ServicePolicy;

class AuditHttpServerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    policy_.allowed_services = {"auth", "file", "messaging", "gateway"};
    policy_.require_caller_identity = false;
    service_ = std::make_unique<audit::services::AuditService>(repository_,
                                                               policy_);

    audit::http::HttpConfig config;
    config.host = "127.0.0.1";
    config.tls_enabled = false;
    server_ = std::make_unique<audit::http::AuditHttpServer>(config, *service_);

    port_ = server_->bindToAnyPort("127.0.0.1");
    ASSERT_GT(port_, 0) << "could not bind a test port";

    thread_ = std::thread([this] { server_->listenAfterBind(); });

    client_ = std::make_unique<httplib::Client>("127.0.0.1", port_);
    client_->set_connection_timeout(2, 0);
    client_->set_read_timeout(2, 0);
  }

  void TearDown() override {
    server_->stop();
    if (thread_.joinable()) thread_.join();
  }

  httplib::Result post(const std::string& body) {
    return client_->Post("/audit/events", body, "application/json");
  }

  static std::string eventBody(const std::string& event_type = "file_uploaded",
                               const std::string& service = "file") {
    return R"({"event_type":")" + event_type + R"(","service_name":")" +
           service + R"(","actor_id":"u1","target_id":"f1",)" +
           R"("outcome":"success","metadata":{"size":42}})";
  }

  audit::tests::InMemoryAuditRepository repository_;
  ServicePolicy policy_;
  std::unique_ptr<audit::services::AuditService> service_;
  std::unique_ptr<audit::http::AuditHttpServer> server_;
  std::unique_ptr<httplib::Client> client_;
  std::thread thread_;
  int port_ = 0;
};

TEST_F(AuditHttpServerTest, ReportsHealth) {
  const auto response = client_->Get("/health");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 200);
  EXPECT_NE(response->body.find("\"status\":\"ok\""), std::string::npos);
}

TEST_F(AuditHttpServerTest, AppendsAndReturnsTheSealedEvent) {
  const auto response = post(eventBody());
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 201);
  EXPECT_NE(response->body.find("\"seq\":1"), std::string::npos);
  EXPECT_NE(response->body.find("\"prev_hash\":"), std::string::npos);
  EXPECT_NE(response->body.find("\"hash\":"), std::string::npos);
  EXPECT_FALSE(response->get_header_value("X-Request-Id").empty());
  EXPECT_EQ(repository_.count(), 1);
}

TEST_F(AuditHttpServerTest, EchoesTheRequestId) {
  httplib::Headers headers = {{"X-Request-Id", "req-123"}};
  const auto response = client_->Post("/audit/events", headers, eventBody(),
                                      "application/json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->get_header_value("X-Request-Id"), "req-123");
}

TEST_F(AuditHttpServerTest, ReplaysAClientEventIdIdempotently) {
  const std::string body =
      R"({"event_id":"evt-1","event_type":"file_uploaded","service_name":"file",)" +
      std::string(R"("outcome":"success"})");

  const auto first = post(body);
  const auto second = post(body);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first->status, 201);
  EXPECT_EQ(second->status, 200);
  EXPECT_EQ(repository_.count(), 1);
}

TEST_F(AuditHttpServerTest, RejectsMalformedJson) {
  const auto response = post("{not json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_NE(response->body.find("VALIDATION_ERROR"), std::string::npos);
}

TEST_F(AuditHttpServerTest, RejectsMissingFields) {
  const auto response = post(R"({"event_type":"file_uploaded"})");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
}

TEST_F(AuditHttpServerTest, RejectsUnknownService) {
  const auto response = post(eventBody("file_uploaded", "stranger"));
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 403);
}

TEST_F(AuditHttpServerTest, ListsEventsWithFilters) {
  post(eventBody("file_uploaded", "file"));
  post(eventBody("login_succeeded", "auth"));
  post(eventBody("file_downloaded", "file"));

  const auto all = client_->Get("/audit/events");
  ASSERT_TRUE(all);
  EXPECT_EQ(all->status, 200);
  EXPECT_NE(all->body.find("\"count\":3"), std::string::npos);

  const auto filtered = client_->Get("/audit/events?service_name=auth");
  ASSERT_TRUE(filtered);
  EXPECT_NE(filtered->body.find("\"count\":1"), std::string::npos);

  const auto limited = client_->Get("/audit/events?limit=2&order=asc");
  ASSERT_TRUE(limited);
  EXPECT_NE(limited->body.find("\"count\":2"), std::string::npos);
  EXPECT_NE(limited->body.find("\"seq\":1"), std::string::npos);
}

TEST_F(AuditHttpServerTest, FetchesOneEventById) {
  post(eventBody());
  ASSERT_EQ(repository_.all().size(), 1u);
  const std::string id = repository_.all().front().id;

  const auto found = client_->Get(("/audit/events/" + id).c_str());
  ASSERT_TRUE(found);
  EXPECT_EQ(found->status, 200);
  EXPECT_NE(found->body.find(id), std::string::npos);

  const auto missing = client_->Get("/audit/events/does-not-exist");
  ASSERT_TRUE(missing);
  EXPECT_EQ(missing->status, 404);
}

TEST_F(AuditHttpServerTest, VerifiesTheChainAndReportsTampering) {
  post(eventBody());
  post(eventBody("login_succeeded", "auth"));

  const auto healthy = client_->Get("/audit/verify");
  ASSERT_TRUE(healthy);
  EXPECT_EQ(healthy->status, 200);
  EXPECT_NE(healthy->body.find("\"valid\":true"), std::string::npos);

  auto events = repository_.all();
  auto tampered = events.front();
  tampered.outcome = "failure";
  repository_.overwrite(tampered);

  const auto broken = client_->Get("/audit/verify");
  ASSERT_TRUE(broken);
  EXPECT_NE(broken->body.find("\"valid\":false"), std::string::npos);
  EXPECT_NE(broken->body.find("\"first_broken_seq\":1"), std::string::npos);
}

TEST_F(AuditHttpServerTest, AppendsBatchesInOrder) {
  const std::string body =
      R"({"events":[)" + eventBody("file_uploaded", "file") + "," +
      eventBody("login_succeeded", "auth") + "]}";

  const auto response =
      client_->Post("/audit/events/batch", body, "application/json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 201);
  EXPECT_NE(response->body.find("\"appended\":2"), std::string::npos);
  EXPECT_EQ(repository_.count(), 2);
}

TEST_F(AuditHttpServerTest, ReportsHowMuchOfAFailedBatchWasStored) {
  const std::string invalid =
      R"({"event_type":"login_succeeded","service_name":"auth","outcome":"maybe"})";
  const std::string body =
      R"({"events":[)" + eventBody("file_uploaded", "file") + "," + invalid + "]}";

  const auto response =
      client_->Post("/audit/events/batch", body, "application/json");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 400);
  EXPECT_NE(response->body.find("\"appended\":1"), std::string::npos);
}

TEST_F(AuditHttpServerTest, ReturnsJsonForUnknownRoutes) {
  const auto response = client_->Get("/nope");
  ASSERT_TRUE(response);
  EXPECT_EQ(response->status, 404);
  EXPECT_NE(response->body.find("\"type\":\"NOT_FOUND\""), std::string::npos);
}

}  // namespace
