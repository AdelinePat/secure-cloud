
#include <gtest/gtest.h>
#include <sodium.h>

#include <nlohmann/json.hpp>
#include <string>

#include "auth/HttpRouter.hpp"
#include "auth/crypto/JwtKeyManager.hpp"
#include "auth/crypto/PasswordHasher.hpp"
#include "auth/services/AuthService.hpp"
#include "repositories/InMemorySessionRepository.hpp"
#include "repositories/InMemoryUserRepository.hpp"

namespace http = boost::beast::http;

using json = nlohmann::json;

class HttpRouterTests : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_GE(sodium_init(), 0); }

  auth::repositories::test::InMemoryUserRepository userRepository;
  auth::repositories::test::InMemorySessionRepository sessionRepository;

  auth::crypto::JwtKeyManager jwtKeyManager{
      "/app/auth/tests/fixtures/test-private.pem",
      "/app/auth/tests/fixtures/test-public.pem"};

  auth::services::AuthService authService{userRepository, sessionRepository,
                                          jwtKeyManager};

  auth::HttpRouter router{authService};

  http::request<http::string_body> makeRequest(http::verb method,
                                               const std::string& target,
                                               const std::string& body = "") {
    http::request<http::string_body> request{method, target, 11};

    request.set(http::field::content_type, "application/json");
    request.body() = body;
    request.prepare_payload();

    return request;
  }
};

// Existing health endpoint tests.

TEST_F(HttpRouterTests, HealthGetReturns200) {
  auto response = router.route(makeRequest(http::verb::get, "/health"));

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), R"({"status":"ok","service":"auth"})");
}

TEST_F(HttpRouterTests, HealthPostReturns405) {
  auto response = router.route(makeRequest(http::verb::post, "/health"));

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET");
}

TEST_F(HttpRouterTests, UnknownRouteReturns404) {
  auto response = router.route(makeRequest(http::verb::get, "/unknown"));

  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_EQ(response.body(), R"({"error":"Not Found"})");
}

// Login endpoint tests.

TEST_F(HttpRouterTests, LoginGetReturns405) {
  auto response = router.route(makeRequest(http::verb::get, "/auth/login"));

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "POST");
}

TEST_F(HttpRouterTests, LoginInvalidJsonReturns400) {
  auto response = router.route(
      makeRequest(http::verb::post, "/auth/login", "{invalid-json"));

  EXPECT_EQ(response.result(), http::status::bad_request);
}

TEST_F(HttpRouterTests, LoginMissingPasswordReturns400) {
  auto response = router.route(
      makeRequest(http::verb::post, "/auth/login", R"({"username":"alice"})"));

  EXPECT_EQ(response.result(), http::status::bad_request);
}

TEST_F(HttpRouterTests, LoginEmptyUsernameReturns400) {
  auto response =
      router.route(makeRequest(http::verb::post, "/auth/login",
                               R"({"username":"","password":"password123"})"));

  EXPECT_EQ(response.result(), http::status::bad_request);
}

TEST_F(HttpRouterTests, LoginUnknownUserReturns401) {
  auto response = router.route(
      makeRequest(http::verb::post, "/auth/login",
                  R"({"username":"unknown","password":"password123"})"));

  EXPECT_EQ(response.result(), http::status::unauthorized);
}

TEST_F(HttpRouterTests, LoginSuccessReturnsTokens) {
  // Seed a test user with an Argon2id password hash.
  auth::domain::User user{};

  user.id = "test-user-123";
  user.username = "alice";
  user.email = "alice@example.com";
  user.password_hash = auth::crypto::PasswordHasher::hash("CorrectPassword123");

  userRepository.seed(user);

  auto response = router.route(
      makeRequest(http::verb::post, "/auth/login",
                  R"({"username":"alice","password":"CorrectPassword123"})"));

  ASSERT_EQ(response.result(), http::status::ok);

  const json body = json::parse(response.body());

  ASSERT_TRUE(body.contains("access_token"));
  ASSERT_TRUE(body.contains("refresh_token"));

  EXPECT_TRUE(body["access_token"].is_string());
  EXPECT_TRUE(body["refresh_token"].is_string());

  EXPECT_FALSE(body["access_token"].get<std::string>().empty());
  EXPECT_FALSE(body["refresh_token"].get<std::string>().empty());

  EXPECT_EQ(body["token_type"], "Bearer");
  EXPECT_EQ(body["expires_in"], 900);
}

TEST_F(HttpRouterTests, LoginWrongPasswordReturns401) {
  auth::domain::User user{};

  user.id = "test-user-123";
  user.username = "alice";
  user.email = "alice@example.com";
  user.password_hash = auth::crypto::PasswordHasher::hash("CorrectPassword123");

  userRepository.seed(user);

  auto response = router.route(
      makeRequest(http::verb::post, "/auth/login",
                  R"({"username":"alice","password":"WrongPassword"})"));

  EXPECT_EQ(response.result(), http::status::unauthorized);
}
