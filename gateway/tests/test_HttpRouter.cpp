#include <gtest/gtest.h>

#include "gateway/HttpRouter.hpp"

namespace http = boost::beast::http;

TEST(HttpRouterTests, ReturnsHealthResponse) {
  gateway::HttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{http::verb::get, "/health", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::ok);
}

TEST(HttpRouterTests, ReturnsNotFoundForUnknownRoute) {
  gateway::HttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{http::verb::get, "/unknown", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_EQ(response.body(), R"({"error":"NOT_FOUND"})");
}

TEST(HttpRouterTests, RejectsUnsupportedMethod) {
  gateway::HttpClient httpClient;
  gateway::HttpRouter router(httpClient);

  http::request<http::string_body> request{http::verb::post, "/health", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET");
}
TEST(HttpRouterTests, ConversationRouteRejectsUnsupportedMethods) {
  gateway::HttpClient httpClient;
  gateway::HttpRouter router(httpClient);
  http::request<http::string_body> request{
      http::verb::delete_, "/messaging/conversations", 11};
  const auto response = router.route(request);
  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET, POST");
}

TEST(HttpRouterTests, ReturnsAllowForEachMessagingRoute) {
  gateway::HttpClient httpClient;
  gateway::HttpRouter router(httpClient);
  const char* prefix = "/messaging/conversations/01234567-89ab-4def-8123-456789abcdef";

  for (const auto& [path, allow] :
       {std::pair{std::string("/messaging/identity-key"), "PUT"},
        std::pair{std::string(prefix) + "/peer-key", "GET"},
        std::pair{std::string(prefix) + "/messages", "GET, POST"}}) {
    SCOPED_TRACE(path);
    const http::request<http::string_body> request{http::verb::delete_, path, 11};
    const auto response = router.route(request);
    EXPECT_EQ(response.result(), http::status::method_not_allowed);
    EXPECT_EQ(response[http::field::allow], allow);
  }
}
