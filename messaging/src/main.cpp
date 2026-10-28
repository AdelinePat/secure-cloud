#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <nlohmann/json.hpp>
#include <pqxx/pqxx>
#include <stdexcept>
#include <string>

#include "messaging/repositories/PostgresConversationRepository.hpp"
#include "messaging/security/JwtVerifier.hpp"

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

int main() {
  try {
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
              conversation.name = body["name"].get<std::string>();

              // Add the authenticated creator as an administrator.
              conversation.participants.push_back(
                  messaging::domain::Participant{*subject, "admin", ""});

              // Persist the conversation and its participants atomically.
              try {
                // Persist the conversation and its participants atomically.
                const auto stored = conversationRepository.create(conversation);

                response.result(http::status::created);

                response.body() = nlohmann::json{
                    {"id", stored.id},
                    {"type", stored.type},
                    {"name", stored.name.value_or(
                                 "")}}.dump();

              } catch (const pqxx::sql_error& e) {
                // Log database errors without exposing SQL details to the
                // client.
                std::cerr << "Conversation SQL error: " << e.what()
                          << std::endl;

                response.result(http::status::internal_server_error);
                response.body() = R"({"error":"DATABASE_ERROR"})";

              } catch (const std::exception& e) {
                // Keep the HTTP server running if conversation creation fails.
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
        const auto subject = value.compare(0, prefix.size(), prefix) == 0
                                 ? jwtVerifier.verify(value.substr(prefix.size()))
                                 : std::nullopt;
        if (!subject.has_value()) {
          response.result(http::status::unauthorized);
          response.body() = R"({"error":"UNAUTHORIZED"})";
        } else {
          try {
            auto conversations = nlohmann::json::array();
            // Scope the list to the authenticated user's memberships.
            for (const auto& conversation : conversationRepository.listForUser(*subject)) {
              conversations.push_back({{"id", conversation.id},
                                       {"type", conversation.type},
                                       {"name", conversation.name.value_or("")}});
            }
            response.result(http::status::ok);
            response.body() = conversations.dump();
          } catch (const std::exception& e) {
            std::cerr << "Conversation listing error: " << e.what() << std::endl;
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
