#include "file/http/FileRouter.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "repositories/InMemoryFileRepository.hpp"

namespace beast_http = boost::beast::http;
using file::http::FileRouter;
using file::http::Request;
using file::repositories::test::InMemoryFileRepository;

namespace {

// Accepts exactly one token, mapped to one user.
std::optional<std::string> fakeVerify(const std::string& token) {
  if (token == "valid-token") return std::string("user-1");
  return std::nullopt;
}

Request makeRequest(beast_http::verb method, const std::string& target,
                    const std::string& bearer = "") {
  Request request{method, target, 11};
  if (!bearer.empty()) {
    request.set(beast_http::field::authorization, "Bearer " + bearer);
  }
  return request;
}

class FileRouterTest : public ::testing::Test {
 protected:
  InMemoryFileRepository repository_;
  FileRouter router_{repository_, fakeVerify};
};

}  // namespace

TEST_F(FileRouterTest, HealthReturnsOk) {
  auto response = router_.route(makeRequest(beast_http::verb::get, "/health"));
  EXPECT_EQ(response.result(), beast_http::status::ok);
  EXPECT_EQ(response.body(), R"({"status":"ok","service":"file"})");
  EXPECT_EQ(response[beast_http::field::content_type], "application/json");
}

TEST_F(FileRouterTest, HealthRejectsOtherMethods) {
  auto response = router_.route(makeRequest(beast_http::verb::post, "/health"));
  EXPECT_EQ(response.result(), beast_http::status::method_not_allowed);
}

TEST_F(FileRouterTest, FilesRoutesRequireBearerToken) {
  for (const char* target : {"/files", "/files/abc", "/files/abc/chunks/0"}) {
    auto response = router_.route(makeRequest(beast_http::verb::get, target));
    EXPECT_EQ(response.result(), beast_http::status::unauthorized) << target;
    EXPECT_EQ(response.body(), R"({"error":"UNAUTHORIZED"})");
  }
}

TEST_F(FileRouterTest, FilesRoutesRejectInvalidToken) {
  auto response =
      router_.route(makeRequest(beast_http::verb::get, "/files", "forged"));
  EXPECT_EQ(response.result(), beast_http::status::unauthorized);
}

TEST_F(FileRouterTest, FilesRoutesRejectNonBearerScheme) {
  Request request{beast_http::verb::get, "/files", 11};
  request.set(beast_http::field::authorization, "Basic valid-token");
  EXPECT_EQ(router_.route(request).result(), beast_http::status::unauthorized);
}

TEST_F(FileRouterTest, QueryStringIsIgnoredForMatching) {
  auto response =
      router_.route(makeRequest(beast_http::verb::get, "/health?x=1"));
  EXPECT_EQ(response.result(), beast_http::status::ok);
}

TEST_F(FileRouterTest, UnknownRouteReturnsNotFound) {
  auto response =
      router_.route(makeRequest(beast_http::verb::get, "/nothing-here"));
  EXPECT_EQ(response.result(), beast_http::status::not_found);
}

TEST_F(FileRouterTest, PathThatOnlyStartsWithFilesIsNotAFileRoute) {
  // "/filesystem" must not be treated as a protected /files route.
  auto response =
      router_.route(makeRequest(beast_http::verb::get, "/filesystem"));
  EXPECT_EQ(response.result(), beast_http::status::not_found);
}
