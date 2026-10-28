#include <sodium.h>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <pqxx/except>
#include <pqxx/pqxx>
#include <regex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "messaging/repositories/PostgresConversationRepository.hpp"
#include "messaging/repositories/PostgresMessageRepository.hpp"
#include "messaging/security/JwtVerifier.hpp"

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

using tcp = asio::ip::tcp;

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
    messaging::security::JwtVerifier jwtVerifier(publicKeyPath);

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
      http::request<http::string_body> request;

      // Read the incoming HTTP request.
      http::read(socket, buffer, request);

      http::response<http::string_body> response;

      response.version(request.version());
      response.set(http::field::server, "SecureChat-Messaging");
      response.set(http::field::content_type, "application/json");
      response.keep_alive(false);

      if (request.method() == http::verb::get &&
          request.target() == "/health") {
        response.result(http::status::ok);
        response.body() = R"({"status":"ok","service":"messaging"})";

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
                 std::regex_match(
                     std::string(request.target()),
                     std::regex(
                         R"(^/conversations/[0-9a-fA-F-]{36}/messages$)"))) {
        const std::string target(request.target());

        const std::string conversationId =
            target.substr(std::string("/conversations/").size(), 36);

        const std::regex uuidPattern(
            R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$)");

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

        } else if (!std::regex_match(conversationId, uuidPattern)) {
          response.result(http::status::bad_request);
          response.body() = R"({"error":"INVALID_CONVERSATION_ID"})";

        } else {
          try {
            const auto role =
                conversationRepository.findRole(conversationId, *subject);

            if (!role.has_value()) {
              response.result(http::status::forbidden);
              response.body() = R"({"error":"FORBIDDEN"})";

            } else if (request.body().size() > 2 * 1024 * 1024) {
              response.result(http::status::bad_request);
              response.body() = R"({"error":"MESSAGE_TOO_LARGE"})";

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
                  body["encryption_version"] == 1;

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

                if (!std::regex_match(messageId, uuidPattern) ||
                    !nonce.has_value() ||
                    nonce->size() != crypto_secretbox_NONCEBYTES ||
                    !ciphertext.has_value() ||
                    ciphertext->size() < crypto_secretbox_MACBYTES) {
                  response.result(http::status::bad_request);
                  response.body() = R"({"error":"INVALID_ENCRYPTED_MESSAGE"})";

                } else {
                  messaging::domain::Message message;

                  message.id = messageId;
                  message.conversation_id = conversationId;
                  message.user_id = *subject;
                  message.ciphertext = *ciphertext;
                  message.nonce = nonceBase64;
                  message.encryption_version = 1;

                  const auto stored = messageRepository.create(message);

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
      } else {
        response.result(http::status::not_found);
        response.body() = R"({"error":"Not found"})";
      }

      response.prepare_payload();

      // Send the HTTP response.
      http::write(socket, response);

      beast::error_code error;
      socket.shutdown(tcp::socket::shutdown_both, error);
    }
  } catch (const std::exception& e) {
    std::cerr << "Messaging server error: " << e.what() << std::endl;
    return 1;
  }
}
