#include "messaging/http/HttpRouter.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <exception>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "messaging/domain/Errors.hpp"
#include "messaging/http/HttpResponse.hpp"
#include "messaging/util/Base64.hpp"

namespace messaging::http {

namespace {

namespace http = beast::http;
using json = nlohmann::json;

struct ParsedTarget {
  std::string path;
  std::map<std::string, std::string> query;
};

std::string percentDecode(const std::string& value) {
  std::string decoded;
  decoded.reserve(value.size());

  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const std::string hex = value.substr(i + 1, 2);
      unsigned int byte = 0;
      const auto result =
          std::from_chars(hex.data(), hex.data() + hex.size(), byte, 16);
      if (result.ec == std::errc{}) {
        decoded.push_back(static_cast<char>(byte));
        i += 2;
        continue;
      }
    }
    decoded.push_back(value[i] == '+' ? ' ' : value[i]);
  }

  return decoded;
}

ParsedTarget parseTarget(const std::string& target) {
  ParsedTarget parsed;

  const auto queryStart = target.find('?');
  parsed.path = target.substr(0, queryStart);

  if (queryStart == std::string::npos) return parsed;

  std::string query = target.substr(queryStart + 1);
  std::size_t start = 0;
  while (start <= query.size()) {
    const auto end = query.find('&', start);
    const std::string pair = query.substr(
        start, end == std::string::npos ? std::string::npos : end - start);

    if (!pair.empty()) {
      const auto equals = pair.find('=');
      const std::string key = percentDecode(pair.substr(0, equals));
      const std::string value =
          equals == std::string::npos
              ? std::string{}
              : percentDecode(pair.substr(equals + 1));
      parsed.query[key] = value;
    }

    if (end == std::string::npos) break;
    start = end + 1;
  }

  return parsed;
}

std::vector<std::string> splitPath(const std::string& path) {
  std::vector<std::string> segments;
  std::size_t start = 0;

  while (start < path.size()) {
    while (start < path.size() && path[start] == '/') ++start;
    if (start >= path.size()) break;

    const auto end = path.find('/', start);
    segments.push_back(path.substr(
        start, end == std::string::npos ? std::string::npos : end - start));

    if (end == std::string::npos) break;
    start = end + 1;
  }

  return segments;
}

std::optional<std::string> bearerToken(const HttpRouter::Request& request) {
  const auto header = request[http::field::authorization];
  const std::string value(header.begin(), header.end());

  constexpr std::string_view prefix = "Bearer ";
  if (value.size() <= prefix.size() ||
      value.compare(0, prefix.size(), prefix) != 0) {
    return std::nullopt;
  }

  const std::string token = value.substr(prefix.size());
  if (token.empty()) return std::nullopt;
  return token;
}

HttpRouter::Response methodNotAllowed(const HttpRouter::Request& request,
                                      const std::string& allow) {
  auto response = HttpResponse::error(http::status::method_not_allowed,
                                      request.version(), "METHOD_NOT_ALLOWED",
                                      "Method not allowed");
  response.set(http::field::allow, allow);
  return response;
}

json participantJson(const domain::Participant& participant) {
  return {{"user_id", participant.user_id},
          {"role", participant.role},
          {"joined_at", participant.joined_at}};
}

json conversationJson(const domain::Conversation& conversation) {
  json participants = json::array();
  for (const auto& participant : conversation.participants) {
    participants.push_back(participantJson(participant));
  }

  json body = {{"conversation_id", conversation.id},
               {"type", conversation.type},
               {"name", conversation.name.has_value()
                            ? json(*conversation.name)
                            : json(nullptr)},
               {"participants", participants},
               {"created_at", conversation.created_at}};

  if (conversation.creator_id.has_value()) {
    body["creator_id"] = *conversation.creator_id;
  }

  return body;
}

json createdConversationJson(const domain::Conversation& conversation) {
  json participantIds = json::array();
  for (const auto& participant : conversation.participants) {
    participantIds.push_back(participant.user_id);
  }

  return {{"conversation_id", conversation.id},
          {"type", conversation.type},
          {"name", conversation.name.has_value() ? json(*conversation.name)
                                                 : json(nullptr)},
          {"participant_ids", participantIds},
          {"created_at", conversation.created_at},
          {"creator_id", conversation.creator_id.has_value()
                             ? json(*conversation.creator_id)
                             : json(nullptr)}};
}

json attachmentJson(const domain::Attachment& attachment) {
  return {{"file_id", attachment.file_id},
          {"size_bytes", attachment.size_bytes},
          {"checksum_sha256", attachment.checksum_sha256}};
}

json messageJson(const domain::Message& message) {
  json attachments = json::array();
  for (const auto& attachment : message.attachments) {
    attachments.push_back(attachmentJson(attachment));
  }

  return {{"message_id", message.id},
          {"userId", message.user_id.has_value() ? json(*message.user_id)
                                                 : json(nullptr)},
          {"ciphertext", util::base64Encode(message.ciphertext)},
          {"nonce", message.nonce},
          {"encryption_version", message.encryption_version},
          {"server_timestamp", message.server_timestamp},
          {"attachments", attachments}};
}

std::string requireString(const json& body, const char* field) {
  if (!body.contains(field) || !body[field].is_string()) {
    throw domain::MessagingError(400, "BAD_REQUEST",
                                 std::string(field) + " must be a string");
  }
  return body[field].get<std::string>();
}

std::vector<std::string> requireStringArray(const json& body,
                                            const char* field) {
  if (!body.contains(field) || !body[field].is_array()) {
    throw domain::MessagingError(
        400, "BAD_REQUEST", std::string(field) + " must be an array");
  }

  std::vector<std::string> values;
  for (const auto& value : body[field]) {
    if (!value.is_string()) {
      throw domain::MessagingError(
          400, "BAD_REQUEST",
          std::string(field) + " must only contain strings");
    }
    values.push_back(value.get<std::string>());
  }
  return values;
}

services::CreateConversationInput parseCreateConversation(
    const std::string& rawBody) {
  const json body = json::parse(rawBody);

  if (!body.is_object()) {
    throw domain::MessagingError(400, "BAD_REQUEST",
                                 "body must be a JSON object");
  }

  services::CreateConversationInput input;
  input.type = requireString(body, "type");
  input.participantIds = requireStringArray(body, "participant_ids");

  if (body.contains("name") && !body["name"].is_null()) {
    if (!body["name"].is_string()) {
      throw domain::MessagingError(400, "BAD_REQUEST",
                                   "name must be a string or null");
    }
    input.name = body["name"].get<std::string>();
  }

  return input;
}

std::vector<domain::Attachment> parseAttachments(const json& body) {
  std::vector<domain::Attachment> attachments;
  if (!body.contains("attachments")) return attachments;

  if (!body["attachments"].is_array()) {
    throw domain::MessagingError(400, "BAD_REQUEST",
                                 "attachments must be an array");
  }

  for (const auto& value : body["attachments"]) {
    if (!value.is_object()) {
      throw domain::MessagingError(400, "BAD_REQUEST",
                                   "each attachment must be an object");
    }

    domain::Attachment attachment;
    attachment.file_id = requireString(value, "file_id");

    if (value.contains("size_bytes")) {
      if (!value["size_bytes"].is_number_integer()) {
        throw domain::MessagingError(400, "BAD_REQUEST",
                                     "size_bytes must be an integer");
      }
      attachment.size_bytes = value["size_bytes"].get<std::int64_t>();
    }

    if (value.contains("checksum_sha256") &&
        value["checksum_sha256"].is_string()) {
      attachment.checksum_sha256 =
          value["checksum_sha256"].get<std::string>();
    }

    attachments.push_back(std::move(attachment));
  }

  return attachments;
}

services::CreateMessageInput parseCreateMessage(const std::string& rawBody) {
  const json body = json::parse(rawBody);

  if (!body.is_object()) {
    throw domain::MessagingError(400, "BAD_REQUEST",
                                 "body must be a JSON object");
  }

  services::CreateMessageInput input;
  input.messageId = requireString(body, "message_id");
  input.nonce = requireString(body, "nonce");

  const std::string ciphertext = requireString(body, "ciphertext");
  try {
    input.ciphertext = util::base64Decode(ciphertext);
  } catch (const std::invalid_argument&) {
    throw domain::MessagingError(400, "BAD_REQUEST",
                                 "ciphertext must be base64");
  }

  if (body.contains("encryption_version")) {
    if (!body["encryption_version"].is_number_integer()) {
      throw domain::MessagingError(400, "BAD_REQUEST",
                                   "encryption_version must be an integer");
    }
    input.encryptionVersion = body["encryption_version"].get<int>();
  }

  if (body.contains("client_timestamp") &&
      body["client_timestamp"].is_string()) {
    input.clientTimestamp = body["client_timestamp"].get<std::string>();
  }

  input.attachments = parseAttachments(body);
  return input;
}

int parseLimit(const ParsedTarget& target) {
  const auto it = target.query.find("limit");
  if (it == target.query.end() || it->second.empty()) return 50;

  int limit = 0;
  const auto result = std::from_chars(
      it->second.data(), it->second.data() + it->second.size(), limit);

  if (result.ec != std::errc{} ||
      result.ptr != it->second.data() + it->second.size() || limit <= 0) {
    throw domain::MessagingError(400, "BAD_REQUEST", "invalid limit");
  }

  return limit;
}

}  // namespace

HttpRouter::HttpRouter(services::MessagingService& messagingService,
                       crypto::IJwtVerifier& jwtVerifier)
    : messagingService_(messagingService), jwtVerifier_(jwtVerifier) {}

HttpRouter::Response HttpRouter::route(const Request& request) const {
  const ParsedTarget target = parseTarget(request.target());

  if (target.path == "/health") {
    if (request.method() != http::verb::get) {
      return methodNotAllowed(request, "GET");
    }
    return HttpResponse::health(request.version());
  }

  try {
    const auto token = bearerToken(request);
    if (!token.has_value()) {
      return HttpResponse::error(http::status::unauthorized, request.version(),
                                 "UNAUTHORIZED",
                                 "Missing or malformed Authorization header");
    }

    const auto userId = jwtVerifier_.verify(*token);
    if (!userId.has_value()) {
      return HttpResponse::error(http::status::unauthorized, request.version(),
                                 "UNAUTHORIZED", "Invalid or expired token");
    }

    return routeAuthenticated(request, *userId);

  } catch (const domain::MessagingError& error) {
    return HttpResponse::error(static_cast<http::status>(error.status()),
                               request.version(), error.code(), error.what());

  } catch (const json::exception&) {
    return HttpResponse::error(http::status::bad_request, request.version(),
                               "BAD_REQUEST", "Invalid JSON request");

  } catch (const std::exception& exception) {
    std::cerr << "Messaging request error: " << exception.what() << '\n';
    return HttpResponse::error(http::status::internal_server_error,
                               request.version(), "INTERNAL_SERVER_ERROR",
                               "Internal server error");
  }
}

HttpRouter::Response HttpRouter::routeAuthenticated(
    const Request& request, const std::string& userId) const {
  const ParsedTarget target = parseTarget(request.target());
  const std::vector<std::string> segments = splitPath(target.path);

  if (segments.empty() || segments[0] != "conversations") {
    return HttpResponse::error(http::status::not_found, request.version(),
                               "NOT_FOUND", "Not found");
  }

  // /conversations
  if (segments.size() == 1) {
    if (request.method() == http::verb::get) {
      const auto conversations = messagingService_.listConversations(userId);

      json list = json::array();
      for (const auto& conversation : conversations) {
        json participantIds = json::array();
        for (const auto& participant : conversation.participants) {
          participantIds.push_back(participant.user_id);
        }

        list.push_back(
            {{"conversation_id", conversation.id},
             {"type", conversation.type},
             {"name", conversation.name.has_value()
                          ? json(*conversation.name)
                          : json(nullptr)},
             {"participant_ids", participantIds},
             {"last_message_at",
              conversation.last_message_at.has_value()
                  ? json(*conversation.last_message_at)
                  : json(nullptr)},
             {"unread_count", conversation.unread_count}});
      }

      return HttpResponse::json(http::status::ok, request.version(),
                                {{"conversations", list}});
    }

    if (request.method() == http::verb::post) {
      const auto input = parseCreateConversation(request.body());
      const auto conversation =
          messagingService_.createConversation(userId, input);

      return HttpResponse::json(http::status::created, request.version(),
                                createdConversationJson(conversation));
    }

    return methodNotAllowed(request, "GET, POST");
  }

  const std::string& conversationId = segments[1];

  // /conversations/{id}
  if (segments.size() == 2) {
    if (request.method() == http::verb::get) {
      const auto conversation =
          messagingService_.getConversation(userId, conversationId);
      return HttpResponse::json(http::status::ok, request.version(),
                                conversationJson(conversation));
    }

    if (request.method() == http::verb::delete_) {
      messagingService_.deleteConversation(userId, conversationId);
      Response response{http::status::no_content, request.version()};
      response.prepare_payload();
      return response;
    }

    return methodNotAllowed(request, "GET, DELETE");
  }

  // /conversations/{id}/participants/me
  if (segments.size() == 4 && segments[2] == "participants" &&
      segments[3] == "me") {
    if (request.method() != http::verb::delete_) {
      return methodNotAllowed(request, "DELETE");
    }

    const auto result =
        messagingService_.leaveConversation(userId, conversationId);

    return HttpResponse::json(
        http::status::ok, request.version(),
        {{"conversation_id", result.conversationId},
         {"status", result.status}});
  }

  // /conversations/{id}/participants/{userId}
  if (segments.size() == 4 && segments[2] == "participants") {
    if (request.method() != http::verb::delete_) {
      return methodNotAllowed(request, "DELETE");
    }

    const auto result = messagingService_.removeParticipant(
        userId, conversationId, segments[3]);

    return HttpResponse::json(
        http::status::ok, request.version(),
        {{"conversation_id", result.conversationId},
         {"user_id", segments[3]},
         {"status", result.status}});
  }

  // /conversations/{id}/history
  if (segments.size() == 3 && segments[2] == "history") {
    if (request.method() != http::verb::get) {
      return methodNotAllowed(request, "GET");
    }

    std::optional<std::string> before;
    const auto it = target.query.find("before");
    if (it != target.query.end() && !it->second.empty()) {
      before = it->second;
    }

    const auto page = messagingService_.history(
        userId, conversationId, before, parseLimit(target));

    json messages = json::array();
    for (const auto& message : page.messages) {
      messages.push_back(messageJson(message));
    }

    json body = {{"messages", messages}};
    if (page.nextCursor.has_value()) {
      body["next_cursor"] = *page.nextCursor;
    } else {
      body["next_cursor"] = nullptr;
    }

    return HttpResponse::json(http::status::ok, request.version(), body);
  }

  // /conversations/{id}/messages
  if (segments.size() == 3 && segments[2] == "messages") {
    if (request.method() != http::verb::post) {
      return methodNotAllowed(request, "POST");
    }

    const auto input = parseCreateMessage(request.body());
    const auto result =
        messagingService_.sendMessage(userId, conversationId, input);

    return HttpResponse::json(
        http::status::accepted, request.version(),
        {{"message_id", result.messageId},
         {"status", result.status},
         {"server_timestamp", result.serverTimestamp}});
  }

  return HttpResponse::error(http::status::not_found, request.version(),
                             "NOT_FOUND", "Not found");
}

}  // namespace messaging::http
