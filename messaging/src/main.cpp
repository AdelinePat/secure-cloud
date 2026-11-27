#include <sodium.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <pqxx/except>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "common/audit/AuditClient.hpp"
#include "common/jwt/JwtVerifier.hpp"
#include "common/security/TokenFormat.hpp"
#include "messaging/repositories/PostgresConversationRepository.hpp"
#include "messaging/repositories/PostgresMessageRepository.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

std::optional<std::vector<unsigned char>> decodeBase64(
    const std::string& encoded, std::size_t maxDecodedSize) {
  if (encoded.empty() || encoded.size() > maxDecodedSize * 2 + 8) {
    return std::nullopt;
  }

  std::vector<unsigned char> decoded(maxDecodedSize);
  std::size_t decodedLength = 0;
  const char* end = nullptr;

  const int result = sodium_base642bin(
      decoded.data(), decoded.size(), encoded.data(), encoded.size(), nullptr,
      &decodedLength, &end, sodium_base64_VARIANT_ORIGINAL);

  if (result != 0 || end != encoded.data() + encoded.size()) {
    return std::nullopt;
  }

  decoded.resize(decodedLength);
  return decoded;
}

std::string encodeBase64(const std::vector<unsigned char>& data) {
  std::string encoded(
      sodium_base64_ENCODED_LEN(data.size(), sodium_base64_VARIANT_ORIGINAL),
      '\0');

  sodium_bin2base64(encoded.data(), encoded.size(), data.data(), data.size(),
                    sodium_base64_VARIANT_ORIGINAL);

  encoded.resize(std::strlen(encoded.c_str()));
  return encoded;
}

bool validGroupEnvelope(const std::vector<unsigned char>& ciphertext,
                        const messaging::domain::Conversation& conversation,
                        const std::string& sender) {
  if (conversation.participants.size() != 3) {
    return false;
  }

  const auto envelope =
      nlohmann::json::parse(ciphertext.begin(), ciphertext.end(), nullptr, false);
  if (!envelope.is_object() || !envelope.contains("own") ||
      !envelope["own"].is_string() || !envelope.contains("recipients") ||
      !envelope["recipients"].is_object() || envelope["recipients"].size() != 2) {
    return false;
  }

  const auto own = decodeBase64(envelope["own"].get<std::string>(), 1024 * 1024);
  if (!own || own->size() < crypto_secretbox_MACBYTES) {
    return false;
  }

  const auto& recipients = envelope["recipients"];
  for (const auto& participant : conversation.participants) {
    if (participant.user_id == sender) continue;
    const auto entry = recipients.find(participant.user_id);
    if (entry == recipients.end() || !entry->is_object() ||
        !entry->contains("nonce") || !(*entry)["nonce"].is_string() ||
        !entry->contains("ciphertext") || !(*entry)["ciphertext"].is_string()) {
      return false;
    }
    const auto nonce = decodeBase64((*entry)["nonce"].get<std::string>(),
                                    crypto_secretbox_NONCEBYTES);
    const auto box = decodeBase64((*entry)["ciphertext"].get<std::string>(),
                                  1024 * 1024);
    if (!nonce || nonce->size() != crypto_secretbox_NONCEBYTES ||
        !box || box->size() < crypto_secretbox_MACBYTES) {
      return false;
    }
  }
  return true;
}

using tcp = asio::ip::tcp;

namespace {

constexpr std::size_t maxRequestBodySize = 2 * 1024 * 1024;
constexpr std::string_view conversationPrefix = "/conversations/";

std::optional<std::string> conversationRouteId(std::string_view target,
                                               std::string_view suffix) {
  if (!target.starts_with(conversationPrefix) || !target.ends_with(suffix) ||
      target.size() < conversationPrefix.size() + suffix.size()) {
    return std::nullopt;
  }
  const auto id =
      target.substr(conversationPrefix.size(),
                    target.size() - conversationPrefix.size() - suffix.size());
  if (id.find('/') != std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(id);
}

}  // namespace

int main() {
  try {
    if (sodium_init() < 0) {
      throw std::runtime_error("Failed to initialize libsodium");
    }

    const char* publicKeyPath = std::getenv("MESSAGING_JWT_PUBLIC_KEY_PATH");

    if (publicKeyPath == nullptr || publicKeyPath[0] == '\0') {
      throw std::runtime_error(
          "MESSAGING_JWT_PUBLIC_KEY_PATH is not configured");
    }

    // Load the Auth public key once at startup.
    common::jwt::JwtVerifier jwtVerifier(publicKeyPath);

    // Read the PostgreSQL connection settings.
    const char* dbHost = std::getenv("DB_HOST");
    const char* dbPort = std::getenv("DB_PORT");
    const char* dbUser = std::getenv("POSTGRES_USER");
    const char* dbPassword = std::getenv("POSTGRES_PASSWORD");
    const char* dbName = std::getenv("POSTGRES_DB");

    if (!dbHost || !dbPort || !dbUser || !dbPassword || !dbName) {
      throw std::runtime_error("PostgreSQL environment variables are missing");
    }

    // Establish the database connection.
    pqxx::connection dbConnection("host=" + std::string(dbHost) +
                                  " port=" + std::string(dbPort) +
                                  " user=" + std::string(dbUser) +
                                  " password=" + std::string(dbPassword) +
                                  " dbname=" + std::string(dbName));

    // Create the repository using the existing connection.
    messaging::repositories::PostgresConversationRepository
        conversationRepository(dbConnection);

    messaging::repositories::PostgresMessageRepository messageRepository(
        dbConnection);

    std::cout << "Messaging connected to PostgreSQL" << std::endl;

    // Best-effort emitter for security-relevant events. Disabled unless
    // AUDIT_ENABLED is true; a failure never affects message handling.
    const auto auditConfig = common::audit::Config::fromEnv("messaging");
    common::audit::Client auditClient(auditConfig);

    if (auditClient.enabled()) {
      std::cout << "Audit emitter enabled -> " << auditConfig.url << " as "
                << auditClient.serviceName() << std::endl;
    }

    asio::io_context ioContext;

    // Listen on all network interfaces, port 8082.
    tcp::acceptor acceptor(
        ioContext, tcp::endpoint(asio::ip::make_address("0.0.0.0"), 8082));

    std::cout << "Messaging HTTP server listening on port 8082" << std::endl;

    while (true) {
      tcp::socket socket(ioContext);

      // Wait for a client connection.
      acceptor.accept(socket);

      beast::flat_buffer buffer;
      http::request_parser<http::string_body> parser;
      parser.body_limit(maxRequestBodySize);
      beast::error_code error;
      http::read(socket, buffer, parser, error);
      if (error) {
        if (error == http::error::body_limit) {
          const unsigned version =
              parser.is_header_done() ? parser.get().version() : 11;
          http::response<http::string_body> tooLarge{
              http::status::payload_too_large, version};
          tooLarge.set(http::field::content_type, "application/json");
          tooLarge.keep_alive(false);
          tooLarge.body() = R"({"error":"MESSAGE_TOO_LARGE"})";
          tooLarge.prepare_payload();
          http::write(socket, tooLarge, error);
        }
        continue;
      }
      const auto request = parser.release();
      const std::string_view target(request.target().data(),
                                    request.target().size());
      const auto peerId = conversationRouteId(target, "/peer-key");
      const auto groupPeerId = conversationRouteId(target, "/peer-keys");
      const auto messageConversationId =
          conversationRouteId(target, "/messages");

      http::response<http::string_body> response;

      response.version(request.version());
      response.set(http::field::server, "SecureChat-Messaging");
      response.set(http::field::content_type, "application/json");
      response.keep_alive(false);

      if (request.method() == http::verb::get &&
          request.target() == "/health") {
        response.result(http::status::ok);
        response.body() = R"({"status":"ok","service":"messaging"})";

      } else if (request.method() == http::verb::get && groupPeerId) {
        const std::string authorization(request[http::field::authorization]);
        const auto subject = authorization.starts_with("Bearer ")
                                 ? jwtVerifier.verify(authorization.substr(7))
                                 : std::nullopt;
        if (!subject) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";
        } else if (!common::security::isUuid(*groupPeerId)) {
          response.result(http::status::bad_request);
          response.body() = R"({"error":"INVALID_CONVERSATION_ID"})";
        } else {
          const auto& id = *groupPeerId;
          try {
            const auto conversation = conversationRepository.findById(id);

            if (!conversation ||
                !conversationRepository.findRole(id, *subject)) {
              response.result(http::status::forbidden);
              response.body() = R"({"error":"FORBIDDEN"})";
            } else {
              auto peers = nlohmann::json::array();
              bool missingKey = false;
              pqxx::work txn(dbConnection);
              for (const auto& participant : conversation->participants) {
                if (participant.user_id == *subject) {
                  continue;
                }

                const auto rows =
                    txn.exec_params("SELECT public_key FROM users WHERE id=$1",
                                    participant.user_id);
                const auto key = rows.empty() || rows[0][0].is_null()
                                     ? std::string{}
                                     : rows[0][0].as<std::string>();

                const auto decoded =
                    decodeBase64(key, crypto_kx_PUBLICKEYBYTES);
                if (!decoded || decoded->size() != crypto_kx_PUBLICKEYBYTES) {
                  missingKey = true;
                  break;
                }
                peers.push_back(
                    {{"user_id", participant.user_id}, {"public_key", key}});
              }
              txn.commit();
              if (missingKey) {
                response.result(http::status::conflict);
                response.body() = R"({"error":"PEER_KEY_UNAVAILABLE"})";
              } else {
                response.result(http::status::ok);
                response.body() = nlohmann::json{{"peers", peers}}.dump();
              }
            }
          } catch (const std::exception&) {
            std::cerr << "Group peer key lookup failed\n";
            response.result(http::status::internal_server_error);
            response.body() = R"({"error":"INTERNAL_ERROR"})";
          }
        }

      } else if ((request.method() == http::verb::put &&
                  request.target() == "/identity-key") ||
                 (request.method() == http::verb::get && peerId)) {
        const std::string authorization(request[http::field::authorization]);
        const auto subject = authorization.starts_with("Bearer ")
                                 ? jwtVerifier.verify(authorization.substr(7))
                                 : std::nullopt;
        if (!subject) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";
        } else
          try {
            if (request.method() == http::verb::put) {
              const auto body =
                  nlohmann::json::parse(request.body(), nullptr, false);
              const auto key = body.is_object() &&
                                       body.contains("public_key") &&
                                       body["public_key"].is_string()
                                   ? body["public_key"].get<std::string>()
                                   : std::string{};
              const auto decoded = decodeBase64(key, crypto_kx_PUBLICKEYBYTES);
              if (!decoded || decoded->size() != crypto_kx_PUBLICKEYBYTES ||
                  encodeBase64(*decoded) != key) {
                response.result(http::status::bad_request);
                response.body() = R"({"error":"INVALID_PUBLIC_KEY"})";
              } else {
                // First publication is immutable; identical retries are safe.
                pqxx::work txn(dbConnection);
                const auto rows = txn.exec_params(
                    "UPDATE users SET public_key=$1 WHERE id=$2 "
                    "AND (public_key='' OR public_key=$1)",
                    key, *subject);
                const bool stored = rows.affected_rows() == 1;
                txn.commit();
                response.result(stored ? http::status::ok
                                       : http::status::conflict);
                response.body() = stored ? R"({"status":"ok"})"
                                         : R"({"error":"KEY_CONFLICT"})";
              }
            } else if (!common::security::isUuid(*peerId)) {
              response.result(http::status::bad_request);
              response.body() = R"({"error":"INVALID_CONVERSATION_ID"})";
            } else {
              const auto& id = *peerId;
              const auto conversation = conversationRepository.findById(id);
              if (!conversation ||
                  !conversationRepository.findRole(id, *subject)) {
                response.result(http::status::forbidden);
                response.body() = R"({"error":"FORBIDDEN"})";
              } else if (conversation->participants.size() != 2) {
                response.result(http::status::conflict);
                response.body() = R"({"error":"TWO_PARTICIPANTS_REQUIRED"})";
              } else {
                const auto& members = conversation->participants;
                const auto peer = members[0].user_id == *subject
                                      ? members[1].user_id
                                      : members[0].user_id;
                pqxx::work txn(dbConnection);
                const auto rows = txn.exec_params(
                    "SELECT public_key FROM users WHERE id=$1", peer);
                const auto key =
                    rows.empty() ? std::string{} : rows[0][0].as<std::string>();
                txn.commit();
                const auto decoded =
                    decodeBase64(key, crypto_kx_PUBLICKEYBYTES);
                if (!decoded || decoded->size() != crypto_kx_PUBLICKEYBYTES) {
                  response.result(http::status::conflict);
                  response.body() = R"({"error":"PEER_KEY_UNAVAILABLE"})";
                } else {
                  response.result(http::status::ok);
                  response.body() = nlohmann::json{
                      {"user_id", peer},
                      {"public_key",
                       key}}.dump();
                }
              }
            }
          } catch (const std::exception& e) {
            std::cerr << "Identity endpoint failed: " << e.what() << '\n';
            response.result(http::status::internal_server_error);
            response.body() = R"({"error":"INTERNAL_ERROR"})";
          }
      } else if (request.method() == http::verb::post &&
                 request.target() == "/conversations") {
        // Extract the Bearer token.
        const auto authorization = request[http::field::authorization];

        const std::string authorizationValue(authorization.data(),
                                             authorization.size());

        const std::string bearerPrefix = "Bearer ";

        if (authorizationValue.compare(0, bearerPrefix.size(), bearerPrefix) !=
            0) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";

        } else {
          const std::string token =
              authorizationValue.substr(bearerPrefix.size());

          // Verify the JWT before trusting its subject.
          const auto subject = jwtVerifier.verify(token);

          if (!subject.has_value()) {
            response.result(http::status::unauthorized);
            response.body() = R"({"error":"INVALID_TOKEN"})";

          } else {
            // Parse the request body without throwing on malformed JSON.
            const auto body =
                nlohmann::json::parse(request.body(), nullptr, false);

            if (body.is_discarded() || !body.is_object() ||
                !body.contains("type") || !body["type"].is_string() ||
                body["type"] != "group" || !body.contains("name") ||
                !body["name"].is_string() ||
                body["name"].get<std::string>().empty()) {
              response.result(http::status::bad_request);
              response.body() = R"({"error":"INVALID_CONVERSATION_DATA"})";

            } else {
              // Create the conversation using validated request data.
              messaging::domain::Conversation conversation;

              conversation.creator_id = *subject;
              conversation.type = body["type"].get<std::string>();

              if (body.contains("name") && body["name"].is_string()) {
                conversation.name = body["name"].get<std::string>();
              }

              // Add the authenticated creator as administrator.
              conversation.participants.push_back(
                  messaging::domain::Participant{*subject, "admin", ""});

              try {
                bool validParticipants = true;
                std::unordered_set<std::string> participantIds{*subject};

                // Participants are optional for backward compatibility.
                if (body.contains("participants")) {
                  if (!body["participants"].is_array() ||
                      body["participants"].size() > 100) {
                    validParticipants = false;
                  } else {
                    for (const auto& participant : body["participants"]) {
                      if (!participant.is_string()) {
                        validParticipants = false;
                        break;
                      }

                      const std::string username =
                          participant.get<std::string>();

                      if (username.empty() || username.size() > 255) {
                        validParticipants = false;
                        break;
                      }

                      const auto userId =
                          conversationRepository.findUserIdByUsername(username);

                      if (!userId.has_value()) {
                        validParticipants = false;
                        break;
                      }

                      // Prevent duplicates, including the creator.
                      if (participantIds.insert(*userId).second) {
                        conversation.participants.push_back(
                            messaging::domain::Participant{*userId, "member",
                                                           ""});
                      }
                    }
                  }
                }

                if (!validParticipants) {
                  response.result(http::status::bad_request);
                  response.body() = R"({"error":"INVALID_PARTICIPANTS"})";
                } else {
                  const auto stored =
                      conversationRepository.create(conversation);

                  common::audit::Event auditEvent;
                  auditEvent.event_type = "conversation_created";
                  auditEvent.actor_id = *subject;
                  auditEvent.target_id = stored.id;
                  auditEvent.outcome = "success";
                  auditEvent.metadata = {{"type", stored.type},
                                         {"name", stored.name.value_or("")}};
                  auditClient.emit(auditEvent);

                  response.result(http::status::created);
                  response.body() = nlohmann::json{
                      {"id", stored.id},
                      {"type", stored.type},
                      {"name", stored.name.value_or(
                                   "")}}.dump();
                }

              } catch (const pqxx::sql_error& e) {
                std::cerr << "Conversation SQL error: " << e.what()
                          << std::endl;

                response.result(http::status::internal_server_error);
                response.body() = R"({"error":"DATABASE_ERROR"})";

              } catch (const std::exception& e) {
                std::cerr << "Conversation creation error: " << e.what()
                          << std::endl;

                response.result(http::status::internal_server_error);
                response.body() = R"({"error":"INTERNAL_ERROR"})";
              }
            }
          }
        }

      } else if (request.method() == http::verb::get &&
                 request.target() == "/conversations") {
        const auto authorization = request[http::field::authorization];
        const std::string value(authorization.data(), authorization.size());
        const std::string prefix = "Bearer ";
        const auto subject =
            value.compare(0, prefix.size(), prefix) == 0
                ? jwtVerifier.verify(value.substr(prefix.size()))
                : std::nullopt;
        if (!subject.has_value()) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";
        } else {
          try {
            auto conversations = nlohmann::json::array();
            // Scope the list to the authenticated user's memberships.
            for (const auto& conversation :
                 conversationRepository.listForUser(*subject)) {
              conversations.push_back(
                  {{"id", conversation.id},
                   {"type", conversation.type},
                   {"name", conversation.name.value_or("")}});
            }
            response.result(http::status::ok);
            response.body() = conversations.dump();
          } catch (const std::exception& e) {
            std::cerr << "Conversation listing error: " << e.what()
                      << std::endl;
            response.result(http::status::internal_server_error);
            response.body() = R"({"error":"INTERNAL_ERROR"})";
          }
        }
      } else if (request.method() == http::verb::post &&
                 messageConversationId) {
        const auto& conversationId = *messageConversationId;

        const auto authorization = request[http::field::authorization];

        const std::string authValue(authorization.data(), authorization.size());

        const std::string bearerPrefix = "Bearer ";

        const auto subject =
            authValue.compare(0, bearerPrefix.size(), bearerPrefix) == 0
                ? jwtVerifier.verify(authValue.substr(bearerPrefix.size()))
                : std::nullopt;

        if (!subject.has_value()) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";

        } else if (!common::security::isUuid(conversationId)) {
          response.result(http::status::bad_request);
          response.body() = R"({"error":"INVALID_CONVERSATION_ID"})";

        } else {
          try {
            const auto role =
                conversationRepository.findRole(conversationId, *subject);

            if (!role.has_value()) {
              common::audit::Event auditEvent;
              auditEvent.event_type = "message_send_denied";
              auditEvent.actor_id = *subject;
              auditEvent.target_id = conversationId;
              auditEvent.outcome = "failure";
              auditEvent.metadata = {{"reason", "not_a_participant"}};
              auditClient.emit(auditEvent);

              response.result(http::status::forbidden);
              response.body() = R"({"error":"FORBIDDEN"})";

            } else {
              const auto body =
                  nlohmann::json::parse(request.body(), nullptr, false);

              const bool validBody =
                  body.is_object() && body.contains("message_id") &&
                  body["message_id"].is_string() && body.contains("nonce") &&
                  body["nonce"].is_string() && body.contains("ciphertext") &&
                  body["ciphertext"].is_string() &&
                  body.contains("encryption_version") &&
                  body["encryption_version"].is_number_integer() &&
                  body["encryption_version"] >= 1 &&
                  body["encryption_version"] <=
                      4;  // 3: file message, 4: group text envelope

              if (!validBody) {
                response.result(http::status::bad_request);
                response.body() = R"({"error":"INVALID_MESSAGE"})";

              } else {
                const std::string messageId =
                    body["message_id"].get<std::string>();

                const std::string nonceBase64 =
                    body["nonce"].get<std::string>();

                const std::string ciphertextBase64 =
                    body["ciphertext"].get<std::string>();

                const auto nonce =
                    decodeBase64(nonceBase64, crypto_secretbox_NONCEBYTES);

                const auto ciphertext =
                    decodeBase64(ciphertextBase64, 1024 * 1024);

                const auto version = body["encryption_version"].get<int>();
                bool groupEnvelopeValid = true;
                if (version == 4 && ciphertext) {
                  const auto conversation = conversationRepository.findById(conversationId);
                  groupEnvelopeValid = conversation &&
                      validGroupEnvelope(*ciphertext, *conversation, *subject);
                }

                if (!common::security::isUuid(messageId) ||
                    !nonce.has_value() ||
                    nonce->size() != crypto_secretbox_NONCEBYTES ||
                    !ciphertext.has_value() ||
                    ciphertext->size() < crypto_secretbox_MACBYTES ||
                    !groupEnvelopeValid) {
                  response.result(http::status::bad_request);
                  response.body() = R"({"error":"INVALID_ENCRYPTED_MESSAGE"})";

                } else {
                  messaging::domain::Message message;

                  message.id = messageId;
                  message.conversation_id = conversationId;
                  message.user_id = *subject;
                  message.ciphertext = *ciphertext;
                  message.nonce = nonceBase64;
                  message.encryption_version = version;

                  const auto stored = messageRepository.create(message);

                  common::audit::Event auditEvent;
                  auditEvent.event_type = "message_sent";
                  auditEvent.actor_id = *subject;
                  auditEvent.target_id = conversationId;
                  auditEvent.outcome = "success";
                  auditEvent.metadata = {{"message_id", stored.id}};
                  auditClient.emit(auditEvent);

                  response.result(http::status::created);
                  response.body() =
                      nlohmann::json{
                          {"message_id", stored.id},
                          {"conversation_id", stored.conversation_id},
                          {"server_timestamp", stored.server_timestamp},
                          {"status", "sent"}}
                          .dump();
                }
              }
            }

          } catch (const pqxx::sql_error& e) {
            std::cerr << "Message SQL error: " << e.what() << '\n';

            if (e.sqlstate() == "23505") {
              response.result(http::status::conflict);
              response.body() = R"({"error":"DUPLICATE_MESSAGE_ID"})";
            } else {
              response.result(http::status::internal_server_error);
              response.body() = R"({"error":"DATABASE_ERROR"})";
            }

          } catch (const std::exception& e) {
            std::cerr << "Message creation error: " << e.what() << '\n';

            response.result(http::status::internal_server_error);
            response.body() = R"({"error":"INTERNAL_ERROR"})";
          }
        }
      } else if (request.method() == http::verb::get && messageConversationId) {
        const auto& conversationId = *messageConversationId;

        const auto authorization = request[http::field::authorization];
        const std::string authValue(authorization.data(), authorization.size());
        const std::string bearerPrefix = "Bearer ";

        const auto subject =
            authValue.compare(0, bearerPrefix.size(), bearerPrefix) == 0
                ? jwtVerifier.verify(authValue.substr(bearerPrefix.size()))
                : std::nullopt;

        if (!subject.has_value()) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";

        } else if (!common::security::isUuid(conversationId)) {
          response.result(http::status::bad_request);
          response.body() = R"({"error":"INVALID_CONVERSATION_ID"})";

        } else {
          try {
            const auto role =
                conversationRepository.findRole(conversationId, *subject);

            if (!role.has_value()) {
              response.result(http::status::forbidden);
              response.body() = R"({"error":"FORBIDDEN"})";

            } else {
              const auto page =
                  messageRepository.history(conversationId, std::nullopt, 50);

              auto messages = nlohmann::json::array();

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

              nlohmann::json historyResponse;

              historyResponse["messages"] = messages;

              historyResponse["next_cursor"] =
                  page.nextCursor.has_value() ? nlohmann::json(*page.nextCursor)
                                              : nlohmann::json(nullptr);

              response.result(http::status::ok);
              response.body() = historyResponse.dump();
            }

          } catch (const std::exception& e) {
            std::cerr << "Message history error: " << e.what() << '\n';

            response.result(http::status::internal_server_error);
            response.body() = R"({"error":"INTERNAL_ERROR"})";
          }
        }
      } else {
        response.result(http::status::not_found);
        response.body() = R"({"error":"Not found"})";
      }

      response.prepare_payload();

      // Send the HTTP response.
      http::write(socket, response, error);
      socket.shutdown(tcp::socket::shutdown_both, error);
    }
  } catch (const std::exception& e) {
    std::cerr << "Messaging server error: " << e.what() << std::endl;
    return 1;
  }
}
