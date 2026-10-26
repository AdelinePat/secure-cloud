#include <gtest/gtest.h>

#include "audit/json/Json.hpp"

namespace {

std::string canonical(const std::string& input) {
  std::string error;
  const auto result = audit::json::canonicalizeObject(input, error);
  EXPECT_TRUE(result.has_value()) << error;
  return result.value_or("");
}

}  // namespace

TEST(JsonTest, SortsKeysAndRemovesWhitespace) {
  EXPECT_EQ(canonical(R"({ "b": 1, "a": { "z": true, "y": [1, 2] } })"),
            R"({"a":{"y":[1,2],"z":true},"b":1})");
}

TEST(JsonTest, PreservesArraysInOrder) {
  EXPECT_EQ(canonical(R"({"items":[3,"two",{"b":1,"a":2}]})"),
            R"({"items":[3,"two",{"a":2,"b":1}]})");
}

TEST(JsonTest, EscapesStrings) {
  EXPECT_EQ(canonical(R"({"msg":"a\"b\\c\n"})"),
            R"({"msg":"a\"b\\c\n"})");
}

TEST(JsonTest, KeepsDoublesAsDoubles) {
  EXPECT_EQ(canonical(R"({"x":1.0,"y":1.5,"z":2})"),
            R"({"x":1.0,"y":1.5,"z":2})");
}

TEST(JsonTest, RejectsInvalidJson) {
  std::string error;
  EXPECT_FALSE(audit::json::canonicalizeObject("{not json", error).has_value());
  EXPECT_FALSE(error.empty());
}

TEST(JsonTest, RejectsNonObject) {
  std::string error;
  EXPECT_FALSE(audit::json::canonicalizeObject("[1,2,3]", error).has_value());
}

TEST(JsonTest, RejectsDuplicateKeys) {
  std::string error;
  EXPECT_FALSE(
      audit::json::canonicalizeObject(R"({"a":1,"a":2})", error).has_value());
  EXPECT_NE(error.find("duplicate"), std::string::npos);
}

TEST(JsonTest, QuoteEscapesControlCharacters) {
  EXPECT_EQ(audit::json::quote("a\nb\"c"), R"("a\nb\"c")");
}
