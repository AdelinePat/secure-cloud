#include "file/http/FileRouter.hpp"

#include <gtest/gtest.h>

#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "TempDir.hpp"
#include "file/storage/ChunkStorage.hpp"
#include "repositories/InMemoryFileRepository.hpp"

namespace beast_http = boost::beast::http;
using beast_http::status;
using beast_http::verb;
using file::http::FileRouter;
using file::http::kMaxChunkBytes;
using file::http::Request;
using file::http::Response;
using file::repositories::test::InMemoryFileRepository;
using nlohmann::json;

namespace {

const std::string kConversation = "22222222-2222-4222-8222-222222222222";
const std::string kOtherConversation = "33333333-3333-4333-8333-333333333333";
const std::string kAlice = "aaaaaaaa-0000-4000-8000-000000000001";  // admin
const std::string kBob = "bbbbbbbb-0000-4000-8000-000000000002";    // member
const std::string kEve = "eeeeeeee-0000-4000-8000-000000000003";    // outsider
const std::string kUnknownFile = "99999999-9999-4999-8999-999999999999";

// Token "alice" -> kAlice, etc. Anything else is invalid.
std::optional<std::string> fakeVerify(const std::string& token) {
  static const std::map<std::string, std::string> users = {
      {"alice", kAlice}, {"bob", kBob}, {"eve", kEve}};
  auto it = users.find(token);
  if (it == users.end()) return std::nullopt;
  return it->second;
}

Request makeRequest(verb method, const std::string& target,
                    const std::string& token = "",
                    const std::string& body = "") {
  Request request{method, target, 11};
  if (!token.empty()) {
    request.set(beast_http::field::authorization, "Bearer " + token);
  }
  request.body() = body;
  return request;
}

json bodyOf(const Response& response) { return json::parse(response.body()); }

class FileRouterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    repository_.addParticipant(kConversation, kAlice, "admin");
    repository_.addParticipant(kConversation, kBob, "member");
  }

  Response call(verb method, const std::string& target,
                const std::string& token = "", const std::string& body = "") {
    return router_.route(makeRequest(method, target, token, body));
  }

  // Creates a file as alice and returns its id.
  std::string createFile(int64_t size, int32_t chunks) {
    auto response = call(verb::post, "/files", "alice",
                         json{{"conversation_id", kConversation},
                              {"size_bytes", size},
                              {"chunk_count", chunks}}
                             .dump());
    EXPECT_EQ(response.result(), status::created) << response.body();
    return bodyOf(response)["file_id"].get<std::string>();
  }

  // Creates, uploads and completes a 2-chunk file ("hello" + "world!").
  std::string availableFile() {
    const auto id = createFile(11, 2);
    EXPECT_EQ(call(verb::put, chunk(id, 0), "alice", "hello").result(),
              status::ok);
    EXPECT_EQ(call(verb::put, chunk(id, 1), "alice", "world!").result(),
              status::ok);
    EXPECT_EQ(call(verb::post, "/files/" + id + "/complete", "alice").result(),
              status::ok);
    return id;
  }

  static std::string chunk(const std::string& id, int n) {
    return "/files/" + id + "/chunks/" + std::to_string(n);
  }

  file::test::TempDir dir_;
  InMemoryFileRepository repository_;
  file::storage::FilesystemChunkStorage storage_{dir_.path()};
  FileRouter router_{repository_, storage_, fakeVerify};
};

}  // namespace

// ── routing basics ─────────────────────────────────────────────────────────

TEST_F(FileRouterTest, HealthReturnsOk) {
  auto response = call(verb::get, "/health");
  EXPECT_EQ(response.result(), status::ok);
  EXPECT_EQ(response.body(), R"({"status":"ok","service":"file"})");
  EXPECT_EQ(response[beast_http::field::content_type], "application/json");
}

TEST_F(FileRouterTest, HealthRejectsOtherMethods) {
  EXPECT_EQ(call(verb::post, "/health").result(), status::method_not_allowed);
}

TEST_F(FileRouterTest, FilesRoutesRequireValidBearerToken) {
  for (const char* target : {"/files", "/files/abc", "/files/abc/chunks/0"}) {
    auto response = call(verb::get, target);
    EXPECT_EQ(response.result(), status::unauthorized) << target;
    EXPECT_EQ(response.body(), R"({"error":"UNAUTHORIZED"})");
  }
  EXPECT_EQ(call(verb::get, "/files", "forged").result(), status::unauthorized);

  Request basic{verb::get, "/files", 11};
  basic.set(beast_http::field::authorization, "Basic alice");
  EXPECT_EQ(router_.route(basic).result(), status::unauthorized);
}

TEST_F(FileRouterTest, UnknownRoutesReturnNotFound) {
  EXPECT_EQ(call(verb::get, "/nothing-here").result(), status::not_found);
  EXPECT_EQ(call(verb::get, "/filesystem", "alice").result(),
            status::not_found);
  EXPECT_EQ(call(verb::get, "/files/" + kUnknownFile + "/other", "alice")
                .result(),
            status::not_found);
}

TEST_F(FileRouterTest, WrongMethodReturns405) {
  EXPECT_EQ(call(verb::get, "/files", "alice").result(),
            status::method_not_allowed);
  EXPECT_EQ(call(verb::put, "/files/" + kUnknownFile, "alice").result(),
            status::method_not_allowed);
  EXPECT_EQ(call(verb::get, "/files/" + kUnknownFile + "/complete", "alice")
                .result(),
            status::method_not_allowed);
  EXPECT_EQ(call(verb::post, chunk(kUnknownFile, 0), "alice").result(),
            status::method_not_allowed);
}

TEST_F(FileRouterTest, NonUuidFileIdIsNotFound) {
  EXPECT_EQ(call(verb::get, "/files/not-a-uuid", "alice").result(),
            status::not_found);
  EXPECT_EQ(call(verb::get, "/files/../../etc/passwd", "alice").result(),
            status::not_found);
}

// ── POST /files ────────────────────────────────────────────────────────────

TEST_F(FileRouterTest, CreateReturnsUploadingFile) {
  auto response = call(verb::post, "/files", "alice",
                       json{{"conversation_id", kConversation},
                            {"size_bytes", 3000000},
                            {"chunk_count", 3}}
                           .dump());
  ASSERT_EQ(response.result(), status::created) << response.body();
  auto body = bodyOf(response);
  EXPECT_EQ(body["status"], "UPLOADING");
  EXPECT_EQ(body["conversation_id"], kConversation);
  EXPECT_EQ(body["uploader_id"], kAlice);
  EXPECT_EQ(body["size_bytes"], 3000000);
  EXPECT_EQ(body["chunk_count"], 3);
  EXPECT_EQ(body["encryption_algorithm"], "xchacha20poly1305-secretstream");
  EXPECT_TRUE(body["completed_at"].is_null());
}

TEST_F(FileRouterTest, CreateRequiresConversationMembership) {
  auto response = call(verb::post, "/files", "eve",
                       json{{"conversation_id", kConversation},
                            {"size_bytes", 10},
                            {"chunk_count", 1}}
                           .dump());
  EXPECT_EQ(response.result(), status::forbidden);

  response = call(verb::post, "/files", "alice",
                  json{{"conversation_id", kOtherConversation},
                       {"size_bytes", 10},
                       {"chunk_count", 1}}
                      .dump());
  EXPECT_EQ(response.result(), status::forbidden);
}

TEST_F(FileRouterTest, CreateRejectsInvalidBodies) {
  const json valid = {{"conversation_id", kConversation},
                      {"size_bytes", 100},
                      {"chunk_count", 1}};
  auto with = [&](const char* key, json value) {
    json body = valid;
    body[key] = std::move(value);
    return body.dump();
  };

  for (const std::string& body : {
           std::string("not json"),
           std::string("[]"),
           with("conversation_id", "not-a-uuid"),
           with("conversation_id", 42),
           with("size_bytes", 0),
           with("size_bytes", -5),
           with("size_bytes", "100"),
           with("size_bytes", 1.5),
           with("chunk_count", 0),
           with("chunk_count", 101),  // more chunks than bytes
           with("chunk_count", "1"),
       }) {
    EXPECT_EQ(call(verb::post, "/files", "alice", body).result(),
              status::bad_request)
        << body;
  }

  json missing = valid;
  missing.erase("chunk_count");
  EXPECT_EQ(call(verb::post, "/files", "alice", missing.dump()).result(),
            status::bad_request);
}

TEST_F(FileRouterTest, CreateRejectsSizeUnreachableWithChunkCount) {
  // 3 chunks can hold at most 3 * kMaxChunkBytes bytes.
  auto response = call(verb::post, "/files", "alice",
                       json{{"conversation_id", kConversation},
                            {"size_bytes", 3 * kMaxChunkBytes + 1},
                            {"chunk_count", 3}}
                           .dump());
  EXPECT_EQ(response.result(), status::bad_request);
}

TEST_F(FileRouterTest, CreateRejectsFilesOverTheMaximum) {
  auto response = call(verb::post, "/files", "alice",
                       json{{"conversation_id", kConversation},
                            {"size_bytes", file::http::kMaxFileBytes + 1},
                            {"chunk_count", 4096}}
                           .dump());
  EXPECT_EQ(response.result(), status::payload_too_large);
  EXPECT_EQ(bodyOf(response)["error"], "FILE_TOO_LARGE");

  response = call(verb::post, "/files", "alice",
                  R"({"conversation_id":")" + kConversation +
                      R"(","size_bytes":18446744073709551615,"chunk_count":1})");
  EXPECT_EQ(response.result(), status::payload_too_large);
}

// ── PUT /files/{id}/chunks/{n} ─────────────────────────────────────────────

TEST_F(FileRouterTest, PutChunkStoresBytesAndReturnsDigest) {
  const auto id = createFile(5, 1);
  auto response = call(verb::put, chunk(id, 0), "alice", "hello");
  ASSERT_EQ(response.result(), status::ok) << response.body();
  auto body = bodyOf(response);
  EXPECT_EQ(body["chunk_index"], 0);
  EXPECT_EQ(body["size_bytes"], 5);
  EXPECT_EQ(body["sha256"],
            "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824");
  EXPECT_EQ(storage_.read(id, 0), "hello");
}

TEST_F(FileRouterTest, OnlyTheUploaderCanPutChunks) {
  const auto id = createFile(5, 1);
  EXPECT_EQ(call(verb::put, chunk(id, 0), "bob", "hello").result(),
            status::forbidden);
  EXPECT_EQ(call(verb::put, chunk(id, 0), "eve", "hello").result(),
            status::forbidden);
}

TEST_F(FileRouterTest, PutChunkValidatesIndexAndBody) {
  const auto id = createFile(10, 2);
  EXPECT_EQ(call(verb::put, chunk(id, 2), "alice", "x").result(),
            status::bad_request);  // only 0 and 1 exist
  for (const char* index : {"-1", "01", "+1", "x", "9999999999"}) {
    EXPECT_EQ(call(verb::put, "/files/" + id + "/chunks/" + index, "alice", "x")
                  .result(),
              status::bad_request)
        << index;
  }
  EXPECT_EQ(call(verb::put, chunk(id, 0), "alice", "").result(),
            status::bad_request);
  EXPECT_EQ(call(verb::put, chunk(id, 0), "alice",
                 std::string(kMaxChunkBytes + 1, 'x'))
                .result(),
            status::payload_too_large);
}

TEST_F(FileRouterTest, PutChunkAcceptsMaximumChunk) {
  const auto id = createFile(static_cast<int64_t>(kMaxChunkBytes), 1);
  EXPECT_EQ(call(verb::put, chunk(id, 0), "alice",
                 std::string(kMaxChunkBytes, 'x'))
                .result(),
            status::ok);
}

TEST_F(FileRouterTest, PutChunkOnUnknownFileIsNotFound) {
  EXPECT_EQ(call(verb::put, chunk(kUnknownFile, 0), "alice", "x").result(),
            status::not_found);
}

TEST_F(FileRouterTest, PutChunkAfterCompleteIsRejected) {
  const auto id = availableFile();
  EXPECT_EQ(call(verb::put, chunk(id, 0), "alice", "hello").result(),
            status::conflict);
}

// ── GET /files/{id} ────────────────────────────────────────────────────────

TEST_F(FileRouterTest, GetFileListsReceivedChunksWhileUploading) {
  const auto id = createFile(30, 3);
  call(verb::put, chunk(id, 2), "alice", "0123456789");
  call(verb::put, chunk(id, 0), "alice", "0123456789");

  auto response = call(verb::get, "/files/" + id, "bob");
  ASSERT_EQ(response.result(), status::ok);
  EXPECT_EQ(bodyOf(response)["received_chunks"], json::array({0, 2}));
}

TEST_F(FileRouterTest, GetFileWhenAvailableOmitsReceivedChunks) {
  const auto id = availableFile();
  auto body = bodyOf(call(verb::get, "/files/" + id, "bob"));
  EXPECT_EQ(body["status"], "AVAILABLE");
  EXPECT_FALSE(body["completed_at"].is_null());
  EXPECT_FALSE(body.contains("received_chunks"));
}

TEST_F(FileRouterTest, GetFileRequiresMembership) {
  const auto id = createFile(10, 1);
  EXPECT_EQ(call(verb::get, "/files/" + id, "eve").result(), status::forbidden);
  EXPECT_EQ(call(verb::get, "/files/" + kUnknownFile, "alice").result(),
            status::not_found);
}

// ── POST /files/{id}/complete ──────────────────────────────────────────────

TEST_F(FileRouterTest, CompleteListsMissingChunks) {
  const auto id = createFile(30, 3);
  call(verb::put, chunk(id, 1), "alice", "0123456789");

  auto response = call(verb::post, "/files/" + id + "/complete", "alice");
  ASSERT_EQ(response.result(), status::conflict);
  auto body = bodyOf(response);
  EXPECT_EQ(body["error"], "INCOMPLETE_UPLOAD");
  EXPECT_EQ(body["missing_chunks"], json::array({0, 2}));
}

TEST_F(FileRouterTest, CompleteRejectsSizeMismatch) {
  const auto id = createFile(10, 2);
  call(verb::put, chunk(id, 0), "alice", "abc");
  call(verb::put, chunk(id, 1), "alice", "def");

  auto response = call(verb::post, "/files/" + id + "/complete", "alice");
  ASSERT_EQ(response.result(), status::conflict);
  auto body = bodyOf(response);
  EXPECT_EQ(body["error"], "SIZE_MISMATCH");
  EXPECT_EQ(body["expected_bytes"], 10);
  EXPECT_EQ(body["received_bytes"], 6);
}

TEST_F(FileRouterTest, ResentChunkCountsOnce) {
  const auto id = createFile(10, 2);
  call(verb::put, chunk(id, 0), "alice", "abcde");
  call(verb::put, chunk(id, 0), "alice", "abcde");  // retry after a timeout
  call(verb::put, chunk(id, 1), "alice", "fghij");
  EXPECT_EQ(call(verb::post, "/files/" + id + "/complete", "alice").result(),
            status::ok);
}

TEST_F(FileRouterTest, CompleteMarksAvailableAndIsIdempotent) {
  const auto id = availableFile();
  auto response = call(verb::post, "/files/" + id + "/complete", "alice");
  ASSERT_EQ(response.result(), status::ok);
  EXPECT_EQ(bodyOf(response)["status"], "AVAILABLE");
}

TEST_F(FileRouterTest, OnlyTheUploaderCanComplete) {
  const auto id = createFile(5, 1);
  call(verb::put, chunk(id, 0), "alice", "hello");
  EXPECT_EQ(call(verb::post, "/files/" + id + "/complete", "bob").result(),
            status::forbidden);
}

// ── GET /files/{id}/chunks/{n} ─────────────────────────────────────────────

TEST_F(FileRouterTest, MembersCanDownloadChunks) {
  const auto id = availableFile();
  auto response = call(verb::get, chunk(id, 1), "bob");
  ASSERT_EQ(response.result(), status::ok);
  EXPECT_EQ(response.body(), "world!");
  EXPECT_EQ(response[beast_http::field::content_type],
            "application/octet-stream");
}

TEST_F(FileRouterTest, OutsidersCannotDownloadChunks) {
  const auto id = availableFile();
  EXPECT_EQ(call(verb::get, chunk(id, 0), "eve").result(), status::forbidden);
}

TEST_F(FileRouterTest, DownloadBeforeCompleteIsRejected) {
  const auto id = createFile(5, 1);
  call(verb::put, chunk(id, 0), "alice", "hello");
  EXPECT_EQ(call(verb::get, chunk(id, 0), "bob").result(), status::conflict);
}

TEST_F(FileRouterTest, DownloadOutOfRangeChunkIsRejected) {
  const auto id = availableFile();
  EXPECT_EQ(call(verb::get, chunk(id, 2), "bob").result(),
            status::bad_request);
}

TEST_F(FileRouterTest, MissingChunkOnDiskIsAStorageError) {
  const auto id = availableFile();
  std::filesystem::remove(dir_.path() / id / "1");
  auto response = call(verb::get, chunk(id, 1), "bob");
  EXPECT_EQ(response.result(), status::internal_server_error);
  EXPECT_EQ(bodyOf(response)["error"], "STORAGE_ERROR");
}

// ── DELETE /files/{id} ─────────────────────────────────────────────────────

TEST_F(FileRouterTest, UploaderCanDeleteAndBytesAreRemoved) {
  const auto id = availableFile();
  auto response = call(verb::delete_, "/files/" + id, "alice");
  EXPECT_EQ(response.result(), status::no_content);
  EXPECT_FALSE(std::filesystem::exists(dir_.path() / id));
  EXPECT_EQ(call(verb::get, "/files/" + id, "alice").result(),
            status::not_found);
  EXPECT_EQ(call(verb::get, chunk(id, 0), "bob").result(), status::not_found);
}

TEST_F(FileRouterTest, ConversationAdminCanDeleteSomeoneElsesFile) {
  repository_.addParticipant(kConversation, kEve, "member");
  auto created = call(verb::post, "/files", "eve",
                      json{{"conversation_id", kConversation},
                           {"size_bytes", 5},
                           {"chunk_count", 1}}
                          .dump());
  const auto id = bodyOf(created)["file_id"].get<std::string>();

  EXPECT_EQ(call(verb::delete_, "/files/" + id, "alice").result(),
            status::no_content);
}

TEST_F(FileRouterTest, PlainMemberCannotDeleteSomeoneElsesFile) {
  const auto id = availableFile();
  EXPECT_EQ(call(verb::delete_, "/files/" + id, "bob").result(),
            status::forbidden);
  EXPECT_EQ(call(verb::delete_, "/files/" + id, "eve").result(),
            status::forbidden);
  EXPECT_EQ(call(verb::get, chunk(id, 0), "bob").result(), status::ok);
}
