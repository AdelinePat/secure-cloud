#include "file/http/FileRouter.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace file::http {

namespace {

using nlohmann::json;
using beast_http::status;
using beast_http::verb;

Response jsonResponse(const Request& request, status code, std::string body) {
  Response response{code, request.version()};
  response.set(beast_http::field::server, "SecureChat-File");
  response.set(beast_http::field::content_type, "application/json");
  response.body() = std::move(body);
  return response;
}

Response errorResponse(const Request& request, status code,
                       std::string_view error) {
  return jsonResponse(request, code, json{{"error", std::string(error)}}.dump());
}

// Request target without its query string.
std::string_view pathOf(const Request& request) {
  std::string_view target(request.target().data(), request.target().size());
  return target.substr(0, target.find('?'));
}

std::vector<std::string_view> splitPath(std::string_view path) {
  std::vector<std::string_view> segments;
  while (!path.empty()) {
    if (path.front() == '/') {
      path.remove_prefix(1);
      continue;
    }
    const auto end = path.find('/');
    segments.push_back(path.substr(0, end));
    if (end == std::string_view::npos) break;
    path.remove_prefix(end);
  }
  return segments;
}

bool isUuid(std::string_view value) {
  static const std::regex pattern(
      "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{"
      "12}$");
  return std::regex_match(value.begin(), value.end(), pattern);
}

// Canonical non-negative decimal ("0", "17"; not "017", "+1", "-1").
std::optional<int32_t> parseIndex(std::string_view value) {
  if (value.empty() || value.size() > 9 ||
      (value.size() > 1 && value.front() == '0') ||
      !std::all_of(value.begin(), value.end(),
                   [](unsigned char c) { return std::isdigit(c); })) {
    return std::nullopt;
  }
  return static_cast<int32_t>(std::stol(std::string(value)));
}

// A JSON integer in [1, max], without overflowing on huge unsigned values.
std::optional<int64_t> readPositive(const json& value, int64_t max) {
  if (!value.is_number_integer()) return std::nullopt;
  if (value.is_number_unsigned()) {
    const auto v = value.get<uint64_t>();
    if (v == 0 || v > static_cast<uint64_t>(max)) return std::nullopt;
    return static_cast<int64_t>(v);
  }
  const auto v = value.get<int64_t>();
  if (v <= 0 || v > max) return std::nullopt;
  return v;
}

std::string sha256Hex(std::string_view bytes) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_Digest(bytes.data(), bytes.size(), digest, &length, EVP_sha256(),
                 nullptr) != 1) {
    throw std::runtime_error("SHA-256 failed");
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (unsigned int i = 0; i < length; ++i) {
    out.push_back(hex[digest[i] >> 4]);
    out.push_back(hex[digest[i] & 0x0f]);
  }
  return out;
}

json toJson(const domain::FileRecord& file) {
  return {{"file_id", file.id},
          {"conversation_id", file.conversation_id},
          {"uploader_id", file.user_id ? json(*file.user_id) : json(nullptr)},
          {"status", file.status},
          {"size_bytes", file.size_bytes},
          {"chunk_count", file.chunk_count},
          {"encryption_algorithm", file.encryption_algorithm},
          {"created_at", file.created_at},
          {"completed_at",
           file.completed_at ? json(*file.completed_at) : json(nullptr)}};
}

bool isUploader(const domain::FileRecord& file, const std::string& userId) {
  return file.user_id.has_value() && *file.user_id == userId;
}

}  // namespace

FileRouter::FileRouter(repositories::IFileRepository& repository,
                       storage::IChunkStorage& storage,
                       TokenVerifier verifyToken, AuditEmitter audit)
    : repository_(repository),
      storage_(storage),
      verifyToken_(std::move(verifyToken)),
      audit_(std::move(audit)) {}

void FileRouter::emit(const char* eventType, const std::string& userId,
                      const std::string& targetId, json metadata,
                      const char* outcome) {
  if (!audit_) return;
  common::audit::Event event;
  event.event_type = eventType;
  event.actor_id = userId;
  event.target_id = targetId;
  event.outcome = outcome;
  event.metadata = std::move(metadata);
  audit_(event);
}

Response FileRouter::deny(const Request& request, const std::string& userId,
                          const char* action, const std::string& targetId,
                          const std::string& conversationId,
                          const char* reason) {
  emit("file_access_denied", userId, targetId,
       {{"action", action},
        {"conversation_id", conversationId},
        {"reason", reason}},
       "failure");
  return errorResponse(request, status::forbidden, "FORBIDDEN");
}

FileRouter::Access FileRouter::loadAccessible(const Request& request,
                                              const std::string& fileId,
                                              const std::string& userId,
                                              const char* action) {
  Access access;
  access.file = repository_.findById(fileId);
  if (!access.file) {
    access.error = errorResponse(request, status::not_found, "FILE_NOT_FOUND");
    return access;
  }
  const auto role =
      repository_.findParticipantRole(access.file->conversation_id, userId);
  if (!role) {
    access.error = deny(request, userId, action, fileId,
                        access.file->conversation_id, "not_a_participant");
    return access;
  }
  access.role = *role;
  return access;
}

std::optional<std::string> FileRouter::authenticate(
    const Request& request) const {
  const auto header = request[beast_http::field::authorization];
  constexpr std::string_view prefix = "Bearer ";
  if (header.size() <= prefix.size() ||
      std::string_view(header.data(), prefix.size()) != prefix) {
    return std::nullopt;
  }
  return verifyToken_(std::string(header.substr(prefix.size())));
}

Response FileRouter::route(const Request& request) {
  const std::string_view path = pathOf(request);
  const auto method = request.method();

  if (path == "/health") {
    if (method != verb::get) {
      return errorResponse(request, status::method_not_allowed,
                           "METHOD_NOT_ALLOWED");
    }
    return jsonResponse(request, status::ok,
                        R"({"status":"ok","service":"file"})");
  }

  const auto segments = splitPath(path);
  if (segments.empty() || segments[0] != "files" ||
      !(path == "/files" || path.starts_with("/files/"))) {
    return errorResponse(request, status::not_found, "NOT_FOUND");
  }

  const auto userId = authenticate(request);
  if (!userId) {
    return errorResponse(request, status::unauthorized, "UNAUTHORIZED");
  }

  const auto notAllowed = [&request] {
    return errorResponse(request, status::method_not_allowed,
                         "METHOD_NOT_ALLOWED");
  };

  // POST /files
  if (segments.size() == 1) {
    return method == verb::post ? createFile(request, *userId) : notAllowed();
  }

  const std::string fileId(segments[1]);
  if (!isUuid(fileId)) {
    return errorResponse(request, status::not_found, "FILE_NOT_FOUND");
  }

  // GET / DELETE /files/{id}
  if (segments.size() == 2) {
    if (method == verb::get) return getFile(request, *userId, fileId);
    if (method == verb::delete_) return deleteFile(request, *userId, fileId);
    return notAllowed();
  }

  // POST /files/{id}/complete
  if (segments.size() == 3 && segments[2] == "complete") {
    return method == verb::post ? completeFile(request, *userId, fileId)
                                : notAllowed();
  }

  // PUT / GET /files/{id}/chunks/{n}
  if (segments.size() == 4 && segments[2] == "chunks") {
    const auto index = parseIndex(segments[3]);
    if (!index) {
      return errorResponse(request, status::bad_request, "INVALID_CHUNK_INDEX");
    }
    if (method == verb::put) return putChunk(request, *userId, fileId, *index);
    if (method == verb::get) return getChunk(request, *userId, fileId, *index);
    return notAllowed();
  }

  return errorResponse(request, status::not_found, "NOT_FOUND");
}

Response FileRouter::createFile(const Request& request,
                                const std::string& userId) {
  const auto body = json::parse(request.body(), nullptr, false);
  if (body.is_discarded() || !body.is_object() ||
      !body.contains("conversation_id") ||
      !body["conversation_id"].is_string() ||
      !isUuid(body["conversation_id"].get<std::string>()) ||
      !body.contains("size_bytes") || !body.contains("chunk_count")) {
    return errorResponse(request, status::bad_request, "INVALID_FILE_DATA");
  }

  // nlohmann parses every non-negative integer as unsigned, so this also
  // catches values beyond int64.
  const auto& sizeJson = body["size_bytes"];
  if (sizeJson.is_number_unsigned() &&
      sizeJson.get<uint64_t>() > static_cast<uint64_t>(kMaxFileBytes)) {
    return errorResponse(request, status::payload_too_large, "FILE_TOO_LARGE");
  }

  const auto sizeBytes = readPositive(sizeJson, kMaxFileBytes);
  const auto chunkCount = readPositive(body["chunk_count"],
                                       std::numeric_limits<int32_t>::max());

  // Every chunk holds 1..kMaxChunkBytes bytes, so the announced size must be
  // reachable with the announced number of chunks.
  if (!sizeBytes || !chunkCount || *chunkCount > *sizeBytes ||
      *sizeBytes > *chunkCount * static_cast<int64_t>(kMaxChunkBytes)) {
    return errorResponse(request, status::bad_request, "INVALID_FILE_DATA");
  }

  const auto conversationId = body["conversation_id"].get<std::string>();
  if (!repository_.findParticipantRole(conversationId, userId)) {
    return deny(request, userId, "create", conversationId, conversationId,
                "not_a_participant");
  }

  domain::FileRecord file;
  file.conversation_id = conversationId;
  file.user_id = userId;
  file.size_bytes = *sizeBytes;
  file.chunk_count = static_cast<int32_t>(*chunkCount);

  const auto stored = repository_.create(file);
  emit("file_upload_started", userId, stored.id,
       {{"conversation_id", stored.conversation_id},
        {"size_bytes", stored.size_bytes},
        {"chunk_count", stored.chunk_count}});
  return jsonResponse(request, status::created, toJson(stored).dump());
}

Response FileRouter::getFile(const Request& request, const std::string& userId,
                             const std::string& fileId) {
  auto access = loadAccessible(request, fileId, userId, "metadata");
  if (access.error) return std::move(*access.error);

  auto result = toJson(*access.file);
  if (access.file->status == "UPLOADING") {
    // Lets an interrupted upload resume with only the missing chunks.
    auto received = json::array();
    for (const auto& chunk : repository_.listChunks(fileId)) {
      received.push_back(chunk.index);
    }
    result["received_chunks"] = std::move(received);
  }
  return jsonResponse(request, status::ok, result.dump());
}

Response FileRouter::putChunk(const Request& request, const std::string& userId,
                              const std::string& fileId, int32_t index) {
  auto access = loadAccessible(request, fileId, userId, "upload_chunk");
  if (access.error) return std::move(*access.error);
  const auto& file = *access.file;

  if (!isUploader(file, userId)) {
    return deny(request, userId, "upload_chunk", fileId, file.conversation_id,
                "not_the_uploader");
  }
  if (file.status != "UPLOADING") {
    return errorResponse(request, status::conflict, "FILE_NOT_UPLOADING");
  }
  if (index >= file.chunk_count) {
    return errorResponse(request, status::bad_request, "INVALID_CHUNK_INDEX");
  }

  const std::string& bytes = request.body();
  if (bytes.empty()) {
    return errorResponse(request, status::bad_request, "EMPTY_CHUNK");
  }
  if (bytes.size() > kMaxChunkBytes) {
    return errorResponse(request, status::payload_too_large, "CHUNK_TOO_LARGE");
  }

  const auto digest = sha256Hex(bytes);
  storage_.write(fileId, index, bytes);
  repository_.upsertChunk(
      {fileId, index, static_cast<int32_t>(bytes.size()), digest});

  return jsonResponse(request, status::ok,
                      json{{"chunk_index", index},
                           {"size_bytes", bytes.size()},
                           {"sha256", digest}}
                          .dump());
}

Response FileRouter::completeFile(const Request& request,
                                  const std::string& userId,
                                  const std::string& fileId) {
  auto access = loadAccessible(request, fileId, userId, "complete");
  if (access.error) return std::move(*access.error);
  const auto& file = *access.file;

  if (!isUploader(file, userId)) {
    return deny(request, userId, "complete", fileId, file.conversation_id,
                "not_the_uploader");
  }
  if (file.status == "AVAILABLE") {
    // Retrying after a lost response is harmless.
    return jsonResponse(request, status::ok, toJson(file).dump());
  }
  if (file.status != "UPLOADING") {
    return errorResponse(request, status::conflict, "FILE_NOT_UPLOADING");
  }

  const auto chunks = repository_.listChunks(fileId);
  std::set<int32_t> received;
  int64_t totalBytes = 0;
  for (const auto& chunk : chunks) {
    received.insert(chunk.index);
    totalBytes += chunk.size_bytes;
  }

  auto missing = json::array();
  for (int32_t i = 0; i < file.chunk_count && missing.size() < 100; ++i) {
    if (!received.contains(i)) missing.push_back(i);
  }
  if (!missing.empty()) {
    return jsonResponse(
        request, status::conflict,
        json{{"error", "INCOMPLETE_UPLOAD"}, {"missing_chunks", missing}}
            .dump());
  }
  if (totalBytes != file.size_bytes) {
    return jsonResponse(request, status::conflict,
                        json{{"error", "SIZE_MISMATCH"},
                             {"expected_bytes", file.size_bytes},
                             {"received_bytes", totalBytes}}
                            .dump());
  }

  repository_.updateStatus(fileId, "AVAILABLE");
  emit("file_uploaded", userId, fileId,
       {{"conversation_id", file.conversation_id},
        {"size_bytes", file.size_bytes},
        {"chunk_count", file.chunk_count}});
  const auto updated = repository_.findById(fileId);
  return jsonResponse(request, status::ok,
                      toJson(updated ? *updated : file).dump());
}

Response FileRouter::getChunk(const Request& request, const std::string& userId,
                              const std::string& fileId, int32_t index) {
  auto access = loadAccessible(request, fileId, userId, "download");
  if (access.error) return std::move(*access.error);
  const auto& file = *access.file;

  if (file.status != "AVAILABLE") {
    return errorResponse(request, status::conflict, "FILE_NOT_AVAILABLE");
  }
  if (index >= file.chunk_count) {
    return errorResponse(request, status::bad_request, "INVALID_CHUNK_INDEX");
  }

  auto bytes = storage_.read(fileId, index);
  if (!bytes) {
    std::cerr << "[file] chunk " << index << " of " << fileId
              << " is missing from storage\n";
    return errorResponse(request, status::internal_server_error,
                         "STORAGE_ERROR");
  }

  if (index == 0) {
    emit("file_download_started", userId, fileId,
         {{"conversation_id", file.conversation_id},
          {"size_bytes", file.size_bytes}});
  }

  Response response{status::ok, request.version()};
  response.set(beast_http::field::server, "SecureChat-File");
  response.set(beast_http::field::content_type, "application/octet-stream");
  response.body() = std::move(*bytes);
  return response;
}

Response FileRouter::deleteFile(const Request& request,
                                const std::string& userId,
                                const std::string& fileId) {
  auto access = loadAccessible(request, fileId, userId, "delete");
  if (access.error) return std::move(*access.error);

  const auto& file = *access.file;
  const bool uploader = isUploader(file, userId);
  if (!uploader && access.role != "admin") {
    return deny(request, userId, "delete", fileId, file.conversation_id,
                "not_uploader_or_admin");
  }

  repository_.softDelete(fileId);
  emit("file_deleted", userId, fileId,
       {{"conversation_id", file.conversation_id},
        {"deleted_by", uploader ? "uploader" : "admin"}});
  try {
    storage_.removeFile(fileId);
  } catch (const std::exception& e) {
    // The row is already DELETED, so the bytes are unreachable; log and move
    // on rather than report a failure the client cannot fix.
    std::cerr << "[file] could not remove chunks of " << fileId << ": "
              << e.what() << '\n';
  }

  Response response{status::no_content, request.version()};
  response.set(beast_http::field::server, "SecureChat-File");
  return response;
}

}  // namespace file::http
