#include <gtest/gtest.h>

#include "gateway/HttpResponse.hpp"

TEST(HttpResponseTests, ReturnsHealthyResponse) {
  auto response = gateway::HttpResponse::health();

  EXPECT_EQ(response.result(), boost::beast::http::status::ok);

  EXPECT_EQ(
      response[boost::beast::http::field::content_type],
      "application/json"
  );

  EXPECT_EQ(
      response.body(),
      R"({"status":"ok","service":"gateway"})"
  );
}