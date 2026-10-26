#include <gtest/gtest.h>

#include "auth/HttpRouter.hpp"

namespace http = boost::beast::http;

TEST(HttpRouterTests, HealthGetReturns200) {
  auth::HttpRouter router;

  http::request<http::string_body> request{http::verb::get, "/health", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response.body(), R"({"status":"ok","service":"auth"})");
}

TEST(HttpRouterTests, HealthPostReturns405) {
  auth::HttpRouter router;

  http::request<http::string_body> request{http::verb::post, "/health", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::method_not_allowed);
  EXPECT_EQ(response[http::field::allow], "GET");
}

TEST(HttpRouterTests, UnknownRouteReturns404) {
  auth::HttpRouter router;

  http::request<http::string_body> request{http::verb::get, "/unknown", 11};

  auto response = router.route(request);

  EXPECT_EQ(response.result(), http::status::not_found);
  EXPECT_EQ(response.body(), R"({"error":"Not Found"})");
}