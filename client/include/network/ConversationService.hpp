#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include "network/HttpClient.hpp"

namespace network {

class ConversationService : public QObject {
  Q_OBJECT

 public:
  explicit ConversationService(const QUrl& baseUrl, QObject* parent = nullptr);

  void listConversations(const QString& jwt);

  // Create a group conversation.
  void createConversation(const QString& jwt, const QString& name,
                          const QStringList& participants);

 signals:
  // Emitted when the conversation is successfully created.
  void conversationCreated(const QString& conversationId, const QString& name);

  // Emitted when conversation creation fails.
  void conversationCreationFailed(const QString& error);
  void conversationsLoaded(const QJsonArray& conversations);
  void conversationsLoadFailed(const QString& error);

 private:
  HttpClient httpClient_;
};

}  // namespace network