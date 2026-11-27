#pragma once

#include <functional>
#include <optional>
#include <string>

#include "common/audit/AuditClient.hpp"
#include "messaging/http/HttpServer.hpp"
#include "messaging/repositories/IConversationRepository.hpp"
#include "messaging/repositories/IMessageRepository.hpp"
#include "messaging/repositories/IUserDirectory.hpp"

namespace messaging::http {

// Returns the authenticated user id for a bearer token, or nullopt.
// In production this is common::jwt::JwtVerifier::verify; tests stub it.
using TokenVerifier =
    std::function<std::optional<std::string>(const std::string& token)>;

// Receives security-relevant events (common::audit::Client::emit in
// production). Must not throw; an empty function disables auditing.
using AuditEmitter = std::function<void(const common::audit::Event& event)>;

// The stores one request works with. In production they all share the
// database connection borrowed for that request.
struct Repositories {
  repositories::IConversationRepository& conversations;
  repositories::IMessageRepository& messages;
  repositories::IUserDirectory& users;
};

// Maps HTTP requests to messaging operations. Paths are the messaging
// service's internal ones (the gateway strips its /messaging prefix).
//
//   GET  /health
//   PUT  /identity-key                   publish the caller's public key
//   GET  /conversations/{id}/peer-key    the other member's public key (1:1)
//   POST /conversations                  create a conversation
//   GET  /conversations                  the caller's conversations
//   POST /conversations/{id}/messages    send an encrypted message
//   GET  /conversations/{id}/messages    the 50 most recent messages
//
// A router is cheap to build: main() makes one per request around that
// request's repositories. It holds no state of its own.
//
// Audit events: conversation_created, message_sent, message_send_denied.
class MessagingRouter {
 public:
  MessagingRouter(Repositories repositories, TokenVerifier verifyToken,
                  AuditEmitter audit = {});

  Response route(const Request& request);

 private:
  Response publishIdentityKey(const Request& request,
                              const std::string& userId);
  Response getPeerKey(const Request& request, const std::string& userId,
                      const std::string& conversationId);
  Response createConversation(const Request& request,
                              const std::string& userId);
  Response listConversations(const Request& request,
                             const std::string& userId);
  Response sendMessage(const Request& request, const std::string& userId,
                       const std::string& conversationId);
  Response getHistory(const Request& request, const std::string& userId,
                      const std::string& conversationId);

  // User id from "Authorization: Bearer <jwt>", or nullopt.
  std::optional<std::string> authenticate(const Request& request) const;

  void emit(common::audit::Event event) const;

  Repositories repositories_;
  TokenVerifier verifyToken_;
  AuditEmitter audit_;
};

}  // namespace messaging::http
