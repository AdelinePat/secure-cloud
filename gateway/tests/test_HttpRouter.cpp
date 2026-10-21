#include <gtest/gtest.h>

#include "gateway/HttpRouter.hpp"

namespace http = boost::beast::http;

TEST(HttpRouterTests, ReturnsHealthResponse) {
  http::request<http::string_body> request{http::verb::get, "/health", 11};

  auto response = gateway::HttpRouter::route(request);

  EXPECT_EQ(response.result(), http::status::ok);
}

TEST(HttpRouterTests, ReturnsNotFoundForUnknownRoute) {
  http::request<http::string_body> request{http::verb::get, "/unknown", 11};

  auto response = gateway::HttpRouter::route(request);

  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_EQ(response.body(), R"({"error":"NOT_FOUND"})");
}

TEST(HttpRouterTests, RejectsUnsupportedMethod) {
  http::request<http::string_body> request{http::verb::post, "/health", 11};

  auto response = gateway::HttpRouter::route(request);

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET");
}