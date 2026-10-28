#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "messaging/crypto/JwtVerifier.hpp"
#include "messaging/http/HttpRouter.hpp"
#include "messaging/services/MessagingService.hpp"
#include "repositories/InMemoryConversationRepository.hpp"
#include "repositories/InMemoryMessageRepository.hpp"
#include "repositories/InMemoryUserDirectory.hpp"

namespace {

namespace http = boost::beast::http;
namespace domain = messaging::domain;
using json = nlohmann::json;

using messaging::http::HttpRouter;
using messaging::repositories::test::InMemoryConversationRepository;
using messaging::repositories::test::InMemoryMessageRepository;
using messaging::repositories::test::InMemoryUserDirectory;
using messaging::services::CreateConversationInput;
using messaging::services::MessagingService;

constexpr const char* kAlice = "11111111-1111-1111-1111-111111111111";
constexpr const char* kBob = "22222222-2222-2222-2222-222222222222";
constexpr const char* kCharlie = "33333333-3333-3333-3333-333333333333";

constexpr const char* kMessageOne = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
constexpr const char* kNonce = "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB";

class FakeJwtVerifier : public messaging::crypto::IJwtVerifier {
 public:
  std::optional<std::string> verify(const std::string& token) const override {
    const auto it = subjects.find(token);
    if (it == subjects.end()) return std::nullopt;
    return it->second;
  }

  std::map<std::string, std::string> subjects;
};

struct Fixture {
  InMemoryConversationRepository conversations;
  InMemoryMessageRepository messages;
  InMemoryUserDirectory users;
  MessagingService service{conversations, messages, users};
  FakeJwtVerifier verifier;
  HttpRouter router{service, verifier};

  Fixture() {
    users.add(kAlice);
    users.add(kBob);
    users.add(kCharlie);

    verifier.subjects["alice-token"] = kAlice;
    verifier.subjects["bob-token"] = kBob;
    verifier.subjects["charlie-token"] = kCharlie;
  }

  domain::Conversation createDirect() {
    CreateConversationInput input;
    input.type = "direct";
    input.participantIds = {kBob};
    return service.createConversation(kAlice, input);
  }
};

HttpRouter::Request makeRequest(http::verb method, const std::string& target,
                                const std::string& token = "",
                                const std::string& body = "") {
  HttpRouter::Request request{method, target, 11};

  if (!token.empty()) {
    request.set(http::field::authorization, "Bearer " + token);
  }

  if (!body.empty()) {
    request.set(http::field::content_type, "application/json");
    request.body() = body;
  }

  request.prepare_payload();
  return request;
}

json responseJson(const HttpRouter::Response& response) {
  return json::parse(response.body());
}

}  // namespace

TEST(MessagingHttpRouterTest, HealthDoesNotRequireAuthentication) {
  Fixture fixture;

  const auto response =
      fixture.router.route(makeRequest(http::verb::get, "/health"));

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(responseJson(response)["service"], "messaging");
}

TEST(MessagingHttpRouterTest, RejectsMissingToken) {
  Fixture fixture;

  const auto response =
      fixture.router.route(makeRequest(http::verb::get, "/conversations"));

  EXPECT_EQ(response.result(), http::status::unauthorized);
}

TEST(MessagingHttpRouterTest, RejectsInvalidToken) {
  Fixture fixture;

  const auto response = fixture.router.route(
      makeRequest(http::verb::get, "/conversations", "unknown-token"));

  EXPECT_EQ(response.result(), http::status::unauthorized);
}

TEST(MessagingHttpRouterTest, CreatesDirectConversation) {
  Fixture fixture;

  const auto response = fixture.router.route(makeRequest(
      http::verb::post, "/conversations", "alice-token",
      std::string(R"({"type":"direct","participant_ids":[")") + kBob +
          R"("],"name":null})"));

  ASSERT_EQ(response.result(), http::status::created);

  const json body = responseJson(response);
  EXPECT_EQ(body["type"], "direct");
  EXPECT_EQ(body["creator_id"], kAlice);
  ASSERT_EQ(body["participant_ids"].size(), 2u);
  EXPECT_EQ(body["participant_ids"][0], kAlice);
  EXPECT_EQ(body["participant_ids"][1], kBob);
}

TEST(MessagingHttpRouterTest, ListsConversationsForCaller) {
  Fixture fixture;
  fixture.createDirect();

  const auto response = fixture.router.route(
      makeRequest(http::verb::get, "/conversations", "alice-token"));

  ASSERT_EQ(response.result(), http::status::ok);

  const json body = responseJson(response);
  ASSERT_EQ(body["conversations"].size(), 1u);
  EXPECT_EQ(body["conversations"][0]["participant_ids"].size(), 2u);
  EXPECT_EQ(body["conversations"][0]["unread_count"], 0);
}

TEST(MessagingHttpRouterTest, ReturnsForbiddenForNonParticipant) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const auto response = fixture.router.route(makeRequest(
      http::verb::get, "/conversations/" + conversation.id, "charlie-token"));

  EXPECT_EQ(response.result(), http::status::forbidden);
}

TEST(MessagingHttpRouterTest, SendsEncryptedMessage) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const std::string body =
      std::string(R"({"message_id":")") + kMessageOne +
      R"(","ciphertext":"AQIDBA==","nonce":")" + kNonce +
      R"(","encryption_version":1,"client_timestamp":"2026-09-02T14:32:00Z","attachments":[]})";

  const auto response = fixture.router.route(makeRequest(
      http::verb::post, "/conversations/" + conversation.id + "/messages",
      "alice-token", body));

  ASSERT_EQ(response.result(), http::status::accepted);

  const json responseBody = responseJson(response);
  EXPECT_EQ(responseBody["message_id"], kMessageOne);
  EXPECT_EQ(responseBody["status"], "SENT");
  EXPECT_FALSE(responseBody["server_timestamp"].get<std::string>().empty());
}

TEST(MessagingHttpRouterTest, HistoryReturnsBase64Ciphertext) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const std::string body =
      std::string(R"({"message_id":")") + kMessageOne +
      R"(","ciphertext":"AQIDBA==","nonce":")" + kNonce +
      R"(","encryption_version":1,"client_timestamp":"2026-09-02T14:32:00Z"})";

  fixture.router.route(makeRequest(
      http::verb::post, "/conversations/" + conversation.id + "/messages",
      "alice-token", body));

  const auto response = fixture.router.route(makeRequest(
      http::verb::get, "/conversations/" + conversation.id + "/history",
      "bob-token"));

  ASSERT_EQ(response.result(), http::status::ok);

  const json responseBody = responseJson(response);
  ASSERT_EQ(responseBody["messages"].size(), 1u);
  EXPECT_EQ(responseBody["messages"][0]["message_id"], kMessageOne);
  EXPECT_EQ(responseBody["messages"][0]["userId"], kAlice);
  EXPECT_EQ(responseBody["messages"][0]["ciphertext"], "AQIDBA==");
  EXPECT_EQ(responseBody["messages"][0]["nonce"], kNonce);
}

TEST(MessagingHttpRouterTest, RejectsDuplicateMessage) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const std::string body =
      std::string(R"({"message_id":")") + kMessageOne +
      R"(","ciphertext":"AQIDBA==","nonce":")" + kNonce +
      R"(","encryption_version":1})";

  const auto first = fixture.router.route(makeRequest(
      http::verb::post, "/conversations/" + conversation.id + "/messages",
      "alice-token", body));
  ASSERT_EQ(first.result(), http::status::accepted);

  const auto second = fixture.router.route(makeRequest(
      http::verb::post, "/conversations/" + conversation.id + "/messages",
      "alice-token", body));
  EXPECT_EQ(second.result(), http::status::conflict);
}

TEST(MessagingHttpRouterTest, RejectsMalformedJson) {
  Fixture fixture;

  const auto response = fixture.router.route(makeRequest(
      http::verb::post, "/conversations", "alice-token", "{not-json"));

  EXPECT_EQ(response.result(), http::status::bad_request);
}

TEST(MessagingHttpRouterTest, DeletesConversationAsAdmin) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const auto response = fixture.router.route(makeRequest(
      http::verb::delete_, "/conversations/" + conversation.id,
      "alice-token"));

  EXPECT_EQ(response.result(), http::status::no_content);
  EXPECT_FALSE(fixture.conversations.findById(conversation.id).has_value());
}

TEST(MessagingHttpRouterTest, LeavesConversation) {
  Fixture fixture;
  const auto conversation = fixture.createDirect();

  const auto response = fixture.router.route(makeRequest(
      http::verb::delete_,
      "/conversations/" + conversation.id + "/participants/me", "bob-token"));

  ASSERT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(responseJson(response)["status"], "left");
}

TEST(MessagingHttpRouterTest, UnknownRouteReturnsNotFound) {
  Fixture fixture;

  const auto response = fixture.router.route(
      makeRequest(http::verb::get, "/nope", "alice-token"));

  EXPECT_EQ(response.result(), http::status::not_found);
}
