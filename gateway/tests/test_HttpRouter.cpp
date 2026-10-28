#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

#include "gateway/HttpClient.hpp"
#include "gateway/HttpRouter.hpp"

namespace http = boost::beast::http;

namespace {

class FakeHttpClient : public gateway::IHttpClient {
 public:
  http::response<http::string_body> send(
      const std::string& host, std::uint16_t port,
      const http::request<http::string_body>& request) override {
    called = true;
    lastHost = host;
    lastPort = port;
    lastRequest = request;

    if (throwOnSend) {
      throw std::runtime_error("upstream down");
    }

    return response;
  }

  bool called = false;
  bool throwOnSend = false;
  std::string lastHost;
  std::uint16_t lastPort = 0;
  http::request<http::string_body> lastRequest{http::verb::get, "/", 11};
  http::response<http::string_body> response{http::status::ok, 11};
};

void setMessagingEnv() {
  setenv("MESSAGING_HOST", "messaging", 1);
  setenv("MESSAGING_PORT", "8082", 1);
}

void setAuthEnv() {
  setenv("AUTH_HOST", "auth", 1);
  setenv("AUTH_PORT", "8081", 1);
}

}  // namespace

TEST(HttpRouterTests, ReturnsHealthResponse) {
  FakeHttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{http::verb::get, "/health", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::ok);
}

TEST(HttpRouterTests, ReturnsNotFoundForUnknownRoute) {
  FakeHttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{http::verb::get, "/unknown", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_EQ(response.body(), R"({"error":"NOT_FOUND"})");
}

TEST(HttpRouterTests, RejectsUnsupportedMethod) {
  FakeHttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{http::verb::post, "/health", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET");
}

TEST(HttpRouterTests, ProxiesMessagingRoutesAndPreservesAuthorization) {
  setMessagingEnv();

  FakeHttpClient httpClient;
  httpClient.response = {http::status::accepted, 11};
  httpClient.response.set(http::field::content_type, "application/json");
  httpClient.response.body() = R"({"status":"SENT"})";
  httpClient.response.prepare_payload();

  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{
      http::verb::post, "/secureChat/conversations/abc/messages", 11};
  request.set(http::field::authorization, "Bearer token-123");
  request.set(http::field::content_type, "application/json");
  request.body() = R"({"message_id":"m1"})";
  request.prepare_payload();

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::accepted);
  ASSERT_TRUE(httpClient.called);
  EXPECT_EQ(httpClient.lastHost, "messaging");
  EXPECT_EQ(httpClient.lastPort, 8082);
  EXPECT_EQ(httpClient.lastRequest.method(), http::verb::post);
  EXPECT_EQ(httpClient.lastRequest.target(),
            "/conversations/abc/messages");
  EXPECT_EQ(httpClient.lastRequest[http::field::authorization],
            "Bearer token-123");
  EXPECT_EQ(httpClient.lastRequest.body(), R"({"message_id":"m1"})");
}

TEST(HttpRouterTests, ProxiesMessagingListQueryString) {
  setMessagingEnv();

  FakeHttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{
      http::verb::get, "/secureChat/conversations?limit=20", 11};
  request.set(http::field::authorization, "Bearer token-123");

  router.route(request);

  ASSERT_TRUE(httpClient.called);
  EXPECT_EQ(httpClient.lastRequest.target(), "/conversations?limit=20");
}

TEST(HttpRouterTests, ProxiesAuthLoginAndStripsCredentials) {
  setAuthEnv();

  FakeHttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{
      http::verb::post, "/secureChat/auth/login", 11};
  request.set(http::field::authorization, "Bearer stale-token");
  request.set(http::field::cookie, "session=stale");
  request.body() = R"({"username":"alice","password":"password123"})";
  request.prepare_payload();

  router.route(request);

  ASSERT_TRUE(httpClient.called);
  EXPECT_EQ(httpClient.lastRequest.target(), "/auth/login");
  EXPECT_TRUE(httpClient.lastRequest[http::field::authorization].empty());
  EXPECT_TRUE(httpClient.lastRequest[http::field::cookie].empty());
}

TEST(HttpRouterTests, ReturnsBadGatewayWhenMessagingIsUnavailable) {
  setMessagingEnv();

  FakeHttpClient httpClient;
  httpClient.throwOnSend = true;

  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{
      http::verb::get, "/secureChat/conversations", 11};
  request.set(http::field::authorization, "Bearer token-123");

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::bad_gateway);
  EXPECT_EQ(response.body(), R"({"error":"MESSAGING_SERVICE_UNAVAILABLE"})");
}

TEST(HttpRouterTests, ReturnsBadGatewayWhenMessagingConfigIsMissing) {
  unsetenv("MESSAGING_HOST");
  unsetenv("MESSAGING_PORT");

  FakeHttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{
      http::verb::get, "/secureChat/conversations", 11};
  request.set(http::field::authorization, "Bearer token-123");

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::bad_gateway);
  EXPECT_EQ(response.body(), R"({"error":"MESSAGING_SERVICE_UNAVAILABLE"})");
  EXPECT_FALSE(httpClient.called);
}
