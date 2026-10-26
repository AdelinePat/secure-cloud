#include <gtest/gtest.h>

#include <string>

#include "auth/HttpResponse.hpp"

namespace http = boost::beast::http;

TEST(HttpResponseTests, ReturnsAuthHealthResponse) {
  auto response = auth::HttpResponse::health();

  EXPECT_EQ(response.result(), http::status::ok);
  EXPECT_EQ(response[http::field::content_type], "application/json");
  EXPECT_EQ(response.body(), R"({"status":"ok","service":"auth"})");
  EXPECT_EQ(response[http::field::content_length],
            std::to_string(response.body().size()));
}