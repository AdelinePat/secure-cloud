#include "messaging/http/MessagingRouter.hpp"

#include <sodium.h>

#include <iostream>
#include <nlohmann/json.hpp>
#include <pqxx/except>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "common/security/TokenFormat.hpp"
#include "messaging/util/Base64.hpp"

namespace messaging::http {

namespace {

using beast_http::status;
using beast_http::verb;
using nlohmann::json;
using util::decodeBase64;
using util::encodeBase64;

constexpr std::string_view kConversationPrefix = "/conversations/";
constexpr int kHistoryPageSize = 50;
constexpr std::size_t kMaxParticipants = 100;
constexpr std::size_t kMaxUsernameLength = 255;
constexpr std::size_t kMaxCiphertextBytes = 1024 * 1024;

Response jsonResponse(const Request& request, status code, std::string body) {
  Response response{code, request.version()};
  response.set(beast_http::field::server, "SecureChat-Messaging");
  response.set(beast_http::field::content_type, "application/json");
  response.body() = std::move(body);
  return response;
}

Response errorResponse(const Request& request, status code,
                       std::string_view error) {
  return jsonResponse(request, code,
                      json{{"error", std::string(error)}}.dump());
}

// Request target without its query string.
std::string_view pathOf(const Request& request) {
  std::string_view target(request.target().data(), request.target().size());
  return target.substr(0, target.find('?'));
}

// "{id}" from "/conversations/{id}<suffix>", or nullopt.
std::optional<std::string> conversationRouteId(std::string_view path,
                                               std::string_view suffix) {
  if (!path.starts_with(kConversationPrefix) || !path.ends_with(suffix) ||
      path.size() < kConversationPrefix.size() + suffix.size()) {
    return std::nullopt;
  }
  const auto id =
      path.substr(kConversationPrefix.size(),
                  path.size() - kConversationPrefix.size() - suffix.size());
  if (id.find('/') != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(id);
}

bool isPublicKey(const std::string& key) {
  const auto decoded = decodeBase64(key, crypto_kx_PUBLICKEYBYTES);
  return decoded && decoded->size() == crypto_kx_PUBLICKEYBYTES;
}

}  // namespace

MessagingRouter::MessagingRouter(Repositories repositories,
                                 TokenVerifier verifyToken, AuditEmitter audit)
    : repositories_(repositories),
      verifyToken_(std::move(verifyToken)),
      audit_(std::move(audit)) {}

Response MessagingRouter::route(const Request& request) {
  const auto path = pathOf(request);
  const auto method = request.method();

  if (method == verb::get && path == "/health") {
    return jsonResponse(request, status::ok,
                        R"({"status":"ok","service":"messaging"})");
  }

  const bool isIdentityKey = path == "/identity-key";
  const bool isConversations = path == "/conversations";
  const auto peerKeyId = conversationRouteId(path, "/peer-key");
  const auto messagesId = conversationRouteId(path, "/messages");

  const bool known = (method == verb::put && isIdentityKey) ||
                     (method == verb::get && peerKeyId) ||
                     ((method == verb::post || method == verb::get) &&
                      (isConversations || messagesId));
  if (!known) {
    return errorResponse(request, status::not_found, "Not found");
  }

  const bool hasBearer =
      std::string_view(request[beast_http::field::authorization])
          .starts_with("Bearer ");
  const auto userId = authenticate(request);
  if (!userId) {
    // POST /conversations has always told a bad token apart from a missing one.
    const bool invalidToken = hasBearer && method == verb::post && isConversations;
    return errorResponse(request, status::unauthorized,
                         invalidToken ? "INVALID_TOKEN" : "UNAUTHORIZED");
  }

  if (isIdentityKey) return publishIdentityKey(request, *userId);
  if (peerKeyId) return getPeerKey(request, *userId, *peerKeyId);
  if (isConversations) {
    return method == verb::post ? createConversation(request, *userId)
                                : listConversations(request, *userId);
  }
  return method == verb::post ? sendMessage(request, *userId, *messagesId)
                              : getHistory(request, *userId, *messagesId);
}

std::optional<std::string> MessagingRouter::authenticate(
    const Request& request) const {
  const std::string_view authorization = request[beast_http::field::authorization];
  constexpr std::string_view prefix = "Bearer ";
  if (!authorization.starts_with(prefix)) {
    return std::nullopt;
  }
  return verifyToken_(std::string(authorization.substr(prefix.size())));
}

void MessagingRouter::emit(common::audit::Event event) const {
  if (audit_) {
    audit_(event);
  }
}

Response MessagingRouter::publishIdentityKey(const Request& request,
                                             const std::string& userId) {
  try {
    const auto body = json::parse(request.body(), nullptr, false);
    const auto key = body.is_object() && body.contains("public_key") &&
                             body["public_key"].is_string()
                         ? body["public_key"].get<std::string>()
                         : std::string{};
    const auto decoded = decodeBase64(key, crypto_kx_PUBLICKEYBYTES);
    // Re-encoding rejects non-canonical base64 for the same bytes.
    if (!decoded || decoded->size() != crypto_kx_PUBLICKEYBYTES ||
        encodeBase64(*decoded) != key) {
      return errorResponse(request, status::bad_request, "INVALID_PUBLIC_KEY");
    }

    // First publication is immutable; identical retries are safe.
    if (!repositories_.users.publishIdentityKey(userId, key)) {
      return errorResponse(request, status::conflict, "KEY_CONFLICT");
    }
    return jsonResponse(request, status::ok, R"({"status":"ok"})");
  } catch (const std::exception& e) {
    std::cerr << "Identity endpoint failed: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "INTERNAL_ERROR");
  }
}

Response MessagingRouter::getPeerKey(const Request& request,
                                     const std::string& userId,
                                     const std::string& conversationId) {
  if (!common::security::isUuid(conversationId)) {
    return errorResponse(request, status::bad_request,
                         "INVALID_CONVERSATION_ID");
  }
  try {
    const auto conversation =
        repositories_.conversations.findById(conversationId);
    if (!conversation ||
        !repositories_.conversations.findRole(conversationId, userId)) {
      return errorResponse(request, status::forbidden, "FORBIDDEN");
    }
    const auto& members = conversation->participants;
    if (members.size() != 2) {
      return errorResponse(request, status::conflict,
                           "TWO_PARTICIPANTS_REQUIRED");
    }

    const auto& peer =
        members[0].user_id == userId ? members[1].user_id : members[0].user_id;
    const auto key = repositories_.users.findIdentityKey(peer);
    if (!key || !isPublicKey(*key)) {
      return errorResponse(request, status::conflict, "PEER_KEY_UNAVAILABLE");
    }
    return jsonResponse(request, status::ok,
                        json{{"user_id", peer}, {"public_key", *key}}.dump());
  } catch (const std::exception& e) {
    std::cerr << "Identity endpoint failed: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "INTERNAL_ERROR");
  }
}

Response MessagingRouter::createConversation(const Request& request,
                                             const std::string& userId) {
  const auto body = json::parse(request.body(), nullptr, false);
  if (body.is_discarded() || !body.is_object() || !body.contains("type") ||
      !body["type"].is_string() || body["type"] != "group" ||
      !body.contains("name") || !body["name"].is_string() ||
      body["name"].get<std::string>().empty()) {
    return errorResponse(request, status::bad_request,
                         "INVALID_CONVERSATION_DATA");
  }

  domain::Conversation conversation;
  conversation.creator_id = userId;
  conversation.type = body["type"].get<std::string>();
  conversation.name = body["name"].get<std::string>();
  // The authenticated creator is the conversation's administrator.
  conversation.participants.push_back(
      domain::Participant{userId, "admin", ""});

  try {
    // Participants (usernames) are optional for backward compatibility.
    if (body.contains("participants")) {
      const auto& participants = body["participants"];
      if (!participants.is_array() || participants.size() > kMaxParticipants) {
        return errorResponse(request, status::bad_request,
                             "INVALID_PARTICIPANTS");
      }

      std::unordered_set<std::string> participantIds{userId};
      for (const auto& participant : participants) {
        if (!participant.is_string()) {
          return errorResponse(request, status::bad_request,
                               "INVALID_PARTICIPANTS");
        }
        const auto username = participant.get<std::string>();
        if (username.empty() || username.size() > kMaxUsernameLength) {
          return errorResponse(request, status::bad_request,
                               "INVALID_PARTICIPANTS");
        }
        const auto participantId =
            repositories_.users.findUserIdByUsername(username);
        if (!participantId) {
          return errorResponse(request, status::bad_request,
                               "INVALID_PARTICIPANTS");
        }
        // Skip duplicates, including the creator.
        if (participantIds.insert(*participantId).second) {
          conversation.participants.push_back(
              domain::Participant{*participantId, "member", ""});
        }
      }
    }

    const auto stored = repositories_.conversations.create(conversation);

    common::audit::Event event;
    event.event_type = "conversation_created";
    event.actor_id = userId;
    event.target_id = stored.id;
    event.outcome = "success";
    event.metadata = {{"type", stored.type},
                      {"name", stored.name.value_or("")}};
    emit(std::move(event));

    return jsonResponse(request, status::created,
                        json{{"id", stored.id},
                             {"type", stored.type},
                             {"name", stored.name.value_or("")}}
                            .dump());
  } catch (const pqxx::sql_error& e) {
    std::cerr << "Conversation SQL error: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "DATABASE_ERROR");
  } catch (const std::exception& e) {
    std::cerr << "Conversation creation error: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "INTERNAL_ERROR");
  }
}

Response MessagingRouter::listConversations(const Request& request,
                                            const std::string& userId) {
  try {
    auto conversations = json::array();
    // Scoped to the authenticated user's memberships.
    for (const auto& conversation :
         repositories_.conversations.listForUser(userId)) {
      conversations.push_back({{"id", conversation.id},
                               {"type", conversation.type},
                               {"name", conversation.name.value_or("")}});
    }
    return jsonResponse(request, status::ok, conversations.dump());
  } catch (const std::exception& e) {
    std::cerr << "Conversation listing error: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "INTERNAL_ERROR");
  }
}

Response MessagingRouter::sendMessage(const Request& request,
                                      const std::string& userId,
                                      const std::string& conversationId) {
  if (!common::security::isUuid(conversationId)) {
    return errorResponse(request, status::bad_request,
                         "INVALID_CONVERSATION_ID");
  }

  try {
    if (!repositories_.conversations.findRole(conversationId, userId)) {
      common::audit::Event event;
      event.event_type = "message_send_denied";
      event.actor_id = userId;
      event.target_id = conversationId;
      event.outcome = "failure";
      event.metadata = {{"reason", "not_a_participant"}};
      emit(std::move(event));
      return errorResponse(request, status::forbidden, "FORBIDDEN");
    }

    const auto body = json::parse(request.body(), nullptr, false);
    const bool validBody =
        body.is_object() && body.contains("message_id") &&
        body["message_id"].is_string() && body.contains("nonce") &&
        body["nonce"].is_string() && body.contains("ciphertext") &&
        body["ciphertext"].is_string() && body.contains("encryption_version") &&
        body["encryption_version"].is_number_integer() &&
        body["encryption_version"] >= 1 &&
        body["encryption_version"] <= 3;  // 3: file message (see File_API.md)
    if (!validBody) {
      return errorResponse(request, status::bad_request, "INVALID_MESSAGE");
    }

    const auto messageId = body["message_id"].get<std::string>();
    const auto nonceBase64 = body["nonce"].get<std::string>();
    const auto nonce = decodeBase64(nonceBase64, crypto_secretbox_NONCEBYTES);
    const auto ciphertext = decodeBase64(body["ciphertext"].get<std::string>(),
                                         kMaxCiphertextBytes);
    if (!common::security::isUuid(messageId) || !nonce ||
        nonce->size() != crypto_secretbox_NONCEBYTES || !ciphertext ||
        ciphertext->size() < crypto_secretbox_MACBYTES) {
      return errorResponse(request, status::bad_request,
                           "INVALID_ENCRYPTED_MESSAGE");
    }

    domain::Message message;
    message.id = messageId;
    message.conversation_id = conversationId;
    message.user_id = userId;
    message.ciphertext = *ciphertext;
    message.nonce = nonceBase64;
    message.encryption_version =
        static_cast<int16_t>(body["encryption_version"].get<int>());

    const auto stored = repositories_.messages.create(message);

    common::audit::Event event;
    event.event_type = "message_sent";
    event.actor_id = userId;
    event.target_id = conversationId;
    event.outcome = "success";
    event.metadata = {{"message_id", stored.id}};
    emit(std::move(event));

    return jsonResponse(request, status::created,
                        json{{"message_id", stored.id},
                             {"conversation_id", stored.conversation_id},
                             {"server_timestamp", stored.server_timestamp},
                             {"status", "sent"}}
                            .dump());
  } catch (const pqxx::sql_error& e) {
    std::cerr << "Message SQL error: " << e.what() << '\n';
    if (e.sqlstate() == "23505") {
      return errorResponse(request, status::conflict, "DUPLICATE_MESSAGE_ID");
    }
    return errorResponse(request, status::internal_server_error,
                         "DATABASE_ERROR");
  } catch (const std::exception& e) {
    std::cerr << "Message creation error: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "INTERNAL_ERROR");
  }
}

Response MessagingRouter::getHistory(const Request& request,
                                     const std::string& userId,
                                     const std::string& conversationId) {
  if (!common::security::isUuid(conversationId)) {
    return errorResponse(request, status::bad_request,
                         "INVALID_CONVERSATION_ID");
  }

  try {
    if (!repositories_.conversations.findRole(conversationId, userId)) {
      return errorResponse(request, status::forbidden, "FORBIDDEN");
    }

    const auto page = repositories_.messages.history(
        conversationId, std::nullopt, kHistoryPageSize);

    auto messages = json::array();
    for (const auto& message : page.messages) {
      messages.push_back(
          {{"message_id", message.id},
           {"conversation_id", message.conversation_id},
           {"user_id", message.user_id.value_or("")},
           {"sender_username", message.sender_username.value_or("")},
           {"ciphertext", encodeBase64(message.ciphertext)},
           {"nonce", message.nonce},
           {"encryption_version", message.encryption_version},
           {"server_timestamp", message.server_timestamp}});
    }

    json response;
    response["messages"] = messages;
    response["next_cursor"] =
        page.nextCursor ? json(*page.nextCursor) : json(nullptr);
    return jsonResponse(request, status::ok, response.dump());
  } catch (const std::exception& e) {
    std::cerr << "Message history error: " << e.what() << '\n';
    return errorResponse(request, status::internal_server_error,
                         "INTERNAL_ERROR");
  }
}

}  // namespace messaging::http
