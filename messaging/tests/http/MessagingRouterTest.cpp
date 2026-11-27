#include "messaging/http/MessagingRouter.hpp"

#include <gtest/gtest.h>
#include <sodium.h>

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "repositories/InMemoryConversationRepository.hpp"
#include "repositories/InMemoryMessageRepository.hpp"
#include "repositories/InMemoryUserDirectory.hpp"

namespace beast_http = boost::beast::http;
using beast_http::status;
using beast_http::verb;
using messaging::http::MessagingRouter;
using messaging::http::Request;
using messaging::http::Response;
using messaging::repositories::test::InMemoryConversationRepository;
using messaging::repositories::test::InMemoryMessageRepository;
using messaging::repositories::test::InMemoryUserDirectory;
using nlohmann::json;

namespace {

const std::string kAlice = "aaaaaaaa-0000-4000-8000-000000000001";
const std::string kBob = "bbbbbbbb-0000-4000-8000-000000000002";
const std::string kEve = "eeeeeeee-0000-4000-8000-000000000003";
const std::string kUnknownConversation = "99999999-9999-4999-8999-999999999999";

// base64 of 32, 24 and 16 zero bytes: a key, a nonce and the smallest
// ciphertext (a bare MAC).
const std::string kBobKey = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
const std::string kOtherKey = "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
const std::string kNonce = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
const std::string kCiphertext = "AAAAAAAAAAAAAAAAAAAAAA==";

// Token "alice" -> kAlice, etc. Anything else is invalid.
std::optional<std::string> fakeVerify(const std::string& token) {
  static const std::map<std::string, std::string> users = {
      {"alice", kAlice}, {"bob", kBob}, {"eve", kEve}};
  auto it = users.find(token);
  if (it == users.end()) return std::nullopt;
  return it->second;
}

json bodyOf(const Response& response) { return json::parse(response.body()); }

std::string errorOf(const Response& response) {
  return bodyOf(response).value("error", "");
}

class MessagingRouterTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_GE(sodium_init(), 0); }

  void SetUp() override {
    users_.addUser(kAlice, "alice");
    users_.addUser(kBob, "bob", kBobKey);
    users_.addUser(kEve, "eve");
  }

  Response call(verb method, const std::string& target,
                const std::string& token = "", const std::string& body = "",
                const std::string& scheme = "Bearer ") {
    Request request{method, target, 11};
    if (!token.empty()) {
      request.set(beast_http::field::authorization, scheme + token);
    }
    request.body() = body;
    return router_.route(request);
  }

  // Alice creates a group with bob and returns its id.
  std::string createConversation() {
    auto response =
        call(verb::post, "/conversations", "alice",
             json{{"type", "group"}, {"name", "Team"}, {"participants", {"bob"}}}
                 .dump());
    EXPECT_EQ(response.result(), status::created) << response.body();
    return bodyOf(response)["id"].get<std::string>();
  }

  json message(const std::string& id = "11111111-1111-4111-8111-111111111111") {
    return {{"message_id", id},
            {"nonce", kNonce},
            {"ciphertext", kCiphertext},
            {"encryption_version", 2}};
  }

  InMemoryConversationRepository conversations_;
  InMemoryMessageRepository messages_;
  InMemoryUserDirectory users_;
  std::vector<common::audit::Event> events_;
  MessagingRouter router_{
      {conversations_, messages_, users_}, fakeVerify,
      [this](const common::audit::Event& event) { events_.push_back(event); }};
};

// ── routing & authentication ────────────────────────────────────────────────

TEST_F(MessagingRouterTest, HealthNeedsNoToken) {
  auto response = call(verb::get, "/health");
  EXPECT_EQ(response.result(), status::ok);
  EXPECT_EQ(bodyOf(response)["service"], "messaging");
}

TEST_F(MessagingRouterTest, UnknownRouteIsNotFound) {
  EXPECT_EQ(call(verb::get, "/nope", "alice").result(), status::not_found);
  EXPECT_EQ(call(verb::delete_, "/conversations", "alice").result(),
            status::not_found);
  EXPECT_EQ(call(verb::get, "/conversations/a/b/messages", "alice").result(),
            status::not_found);
}

TEST_F(MessagingRouterTest, RejectsMissingOrInvalidToken) {
  const std::vector<std::string> targets = {
      "/conversations", "/identity-key",
      "/conversations/" + kUnknownConversation + "/messages",
      "/conversations/" + kUnknownConversation + "/peer-key"};
  for (const auto& target : targets) {
    SCOPED_TRACE(target);
    const auto method = target == "/identity-key" ? verb::put : verb::get;
    auto missing = call(method, target);
    EXPECT_EQ(missing.result(), status::unauthorized);
    EXPECT_EQ(errorOf(missing), "UNAUTHORIZED");
    EXPECT_EQ(call(method, target, "forged").result(), status::unauthorized);
    EXPECT_EQ(call(method, target, "alice", "", "Basic ").result(),
              status::unauthorized);
  }
}

TEST_F(MessagingRouterTest, CreateConversationReportsInvalidToken) {
  EXPECT_EQ(errorOf(call(verb::post, "/conversations")), "UNAUTHORIZED");
  EXPECT_EQ(errorOf(call(verb::post, "/conversations", "forged")),
            "INVALID_TOKEN");
}

TEST_F(MessagingRouterTest, IgnoresQueryString) {
  EXPECT_EQ(call(verb::get, "/conversations?limit=5", "alice").result(),
            status::ok);
}

// ── conversations ───────────────────────────────────────────────────────────

TEST_F(MessagingRouterTest, CreatesConversationWithCreatorAsAdmin) {
  const auto id = createConversation();
  EXPECT_EQ(conversations_.findRole(id, kAlice), "admin");
  EXPECT_EQ(conversations_.findRole(id, kBob), "member");
  EXPECT_FALSE(conversations_.findRole(id, kEve));

  ASSERT_EQ(events_.size(), 1u);
  EXPECT_EQ(events_[0].event_type, "conversation_created");
  EXPECT_EQ(events_[0].actor_id, kAlice);
  EXPECT_EQ(events_[0].target_id, id);
}

TEST_F(MessagingRouterTest, IgnoresDuplicateParticipants) {
  auto response = call(
      verb::post, "/conversations", "alice",
      json{{"type", "group"}, {"name", "T"}, {"participants", {"bob", "bob", "alice"}}}
          .dump());
  ASSERT_EQ(response.result(), status::created);
  const auto conversation =
      conversations_.findById(bodyOf(response)["id"].get<std::string>());
  EXPECT_EQ(conversation->participants.size(), 2u);
}

TEST_F(MessagingRouterTest, RejectsInvalidConversationData) {
  for (const auto& body :
       {std::string("not json"), json::array().dump(),
        json{{"type", "direct"}, {"name", "x"}}.dump(),
        json{{"type", "group"}}.dump(),
        json{{"type", "group"}, {"name", ""}}.dump()}) {
    SCOPED_TRACE(body);
    auto response = call(verb::post, "/conversations", "alice", body);
    EXPECT_EQ(response.result(), status::bad_request);
    EXPECT_EQ(errorOf(response), "INVALID_CONVERSATION_DATA");
  }
}

TEST_F(MessagingRouterTest, RejectsInvalidParticipants) {
  for (const auto& participants :
       {json("bob"), json{{"nobody"}}, json{{1}}, json{{""}},
        json(std::vector<std::string>(101, "bob"))}) {
    SCOPED_TRACE(participants.dump());
    auto response = call(
        verb::post, "/conversations", "alice",
        json{{"type", "group"}, {"name", "T"}, {"participants", participants}}
            .dump());
    EXPECT_EQ(response.result(), status::bad_request);
    EXPECT_EQ(errorOf(response), "INVALID_PARTICIPANTS");
  }
  EXPECT_TRUE(conversations_.listForUser(kAlice).empty());
}

TEST_F(MessagingRouterTest, ListsOnlyTheCallersConversations) {
  const auto id = createConversation();

  auto alice = bodyOf(call(verb::get, "/conversations", "alice"));
  ASSERT_EQ(alice.size(), 1u);
  EXPECT_EQ(alice[0]["id"], id);
  EXPECT_EQ(alice[0]["name"], "Team");

  EXPECT_TRUE(bodyOf(call(verb::get, "/conversations", "eve")).empty());
}

// ── messages ────────────────────────────────────────────────────────────────

TEST_F(MessagingRouterTest, SendsAndReadsMessages) {
  const auto id = createConversation();
  const auto path = "/conversations/" + id + "/messages";

  auto sent = call(verb::post, path, "bob", message().dump());
  ASSERT_EQ(sent.result(), status::created) << sent.body();
  EXPECT_EQ(bodyOf(sent)["status"], "sent");
  EXPECT_EQ(events_.back().event_type, "message_sent");

  auto history = call(verb::get, path, "alice");
  ASSERT_EQ(history.result(), status::ok);
  const auto messages = bodyOf(history)["messages"];
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages[0]["user_id"], kBob);
  EXPECT_EQ(messages[0]["ciphertext"], kCiphertext);
  EXPECT_EQ(messages[0]["nonce"], kNonce);
  EXPECT_EQ(messages[0]["encryption_version"], 2);
  EXPECT_TRUE(bodyOf(history)["next_cursor"].is_null());
}

TEST_F(MessagingRouterTest, HistoryReturnsAtMostFiftyMessages) {
  const auto id = createConversation();
  for (int i = 0; i < 51; ++i) {
    char messageId[37];
    std::snprintf(messageId, sizeof messageId,
                  "11111111-1111-4111-8111-%012d", i);
    ASSERT_EQ(call(verb::post, "/conversations/" + id + "/messages", "alice",
                   message(messageId).dump())
                  .result(),
              status::created);
  }
  auto history =
      bodyOf(call(verb::get, "/conversations/" + id + "/messages", "alice"));
  EXPECT_EQ(history["messages"].size(), 50u);
  EXPECT_FALSE(history["next_cursor"].is_null());
}

TEST_F(MessagingRouterTest, OutsiderCannotSendOrRead) {
  const auto id = createConversation();
  const auto path = "/conversations/" + id + "/messages";

  auto sent = call(verb::post, path, "eve", message().dump());
  EXPECT_EQ(sent.result(), status::forbidden);
  EXPECT_EQ(events_.back().event_type, "message_send_denied");
  EXPECT_EQ(events_.back().outcome, "failure");

  EXPECT_EQ(call(verb::get, path, "eve").result(), status::forbidden);
}

TEST_F(MessagingRouterTest, RejectsInvalidConversationId) {
  EXPECT_EQ(errorOf(call(verb::post, "/conversations/abc/messages", "alice",
                         message().dump())),
            "INVALID_CONVERSATION_ID");
  EXPECT_EQ(errorOf(call(verb::get, "/conversations/abc/messages", "alice")),
            "INVALID_CONVERSATION_ID");
}

TEST_F(MessagingRouterTest, RejectsMalformedMessages) {
  const auto path = "/conversations/" + createConversation() + "/messages";

  auto noNonce = message();
  noNonce.erase("nonce");
  auto badVersion = message();
  badVersion["encryption_version"] = 4;
  for (const auto& body : {std::string("{"), noNonce.dump(), badVersion.dump()}) {
    SCOPED_TRACE(body);
    EXPECT_EQ(errorOf(call(verb::post, path, "alice", body)), "INVALID_MESSAGE");
  }

  auto badId = message("not-a-uuid");
  auto shortNonce = message();
  shortNonce["nonce"] = "AAAA";
  auto shortCiphertext = message();
  shortCiphertext["ciphertext"] = "AAAA";
  auto notBase64 = message();
  notBase64["ciphertext"] = "!!!!";
  for (const auto& body : {badId, shortNonce, shortCiphertext, notBase64}) {
    SCOPED_TRACE(body.dump());
    EXPECT_EQ(errorOf(call(verb::post, path, "alice", body.dump())),
              "INVALID_ENCRYPTED_MESSAGE");
  }
}

// ── identity keys ───────────────────────────────────────────────────────────

TEST_F(MessagingRouterTest, PublishesIdentityKeyOnce) {
  const auto body = json{{"public_key", kBobKey}}.dump();
  EXPECT_EQ(call(verb::put, "/identity-key", "alice", body).result(),
            status::ok);
  // Identical retry is fine, a different key is refused.
  EXPECT_EQ(call(verb::put, "/identity-key", "alice", body).result(),
            status::ok);
  auto conflict = call(verb::put, "/identity-key", "alice",
                       json{{"public_key", kOtherKey}}.dump());
  EXPECT_EQ(conflict.result(), status::conflict);
  EXPECT_EQ(errorOf(conflict), "KEY_CONFLICT");
}

TEST_F(MessagingRouterTest, RejectsInvalidIdentityKey) {
  for (const auto& body :
       {std::string("nope"), json{{"public_key", 1}}.dump(),
        json{{"public_key", "AAAA"}}.dump(),
        // Same 32 bytes as kBobKey, but non-canonical (no padding).
        json{{"public_key", kBobKey.substr(0, 43)}}.dump()}) {
    SCOPED_TRACE(body);
    EXPECT_EQ(errorOf(call(verb::put, "/identity-key", "alice", body)),
              "INVALID_PUBLIC_KEY");
  }
}

TEST_F(MessagingRouterTest, ReturnsPeerKeyInOneToOneConversation) {
  const auto path = "/conversations/" + createConversation() + "/peer-key";

  auto response = call(verb::get, path, "alice");
  ASSERT_EQ(response.result(), status::ok) << response.body();
  EXPECT_EQ(bodyOf(response)["user_id"], kBob);
  EXPECT_EQ(bodyOf(response)["public_key"], kBobKey);

  // Alice has not published a key yet.
  EXPECT_EQ(errorOf(call(verb::get, path, "bob")), "PEER_KEY_UNAVAILABLE");
  EXPECT_EQ(call(verb::get, path, "eve").result(), status::forbidden);
}

TEST_F(MessagingRouterTest, PeerKeyNeedsExactlyTwoParticipants) {
  auto created = call(verb::post, "/conversations", "alice",
                      json{{"type", "group"},
                           {"name", "Three"},
                           {"participants", {"bob", "eve"}}}
                          .dump());
  const auto id = bodyOf(created)["id"].get<std::string>();
  EXPECT_EQ(errorOf(call(verb::get, "/conversations/" + id + "/peer-key",
                         "alice")),
            "TWO_PARTICIPANTS_REQUIRED");
  EXPECT_EQ(call(verb::get,
                 "/conversations/" + kUnknownConversation + "/peer-key",
                 "alice")
                .result(),
            status::forbidden);
}

}  // namespace
