#include "network/ConversationService.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace network {

ConversationService::ConversationService(const QUrl& baseUrl, QObject* parent)
    : QObject(parent), httpClient_(baseUrl, this) {}

void ConversationService::listConversations(const QString& jwt) {
  QNetworkReply* reply = httpClient_.listConversations(jwt);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
      emit conversationsLoadFailed(reply->errorString());
      return;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
      emit conversationsLoadFailed("Invalid conversations response");
      return;
    }
    for (const auto& value : document.array()) {
      const auto conversation = value.toObject();
      if (!value.isObject() || !conversation.value("id").isString() ||
          conversation.value("id").toString().isEmpty() ||
          !conversation.value("name").isString()) {
        emit conversationsLoadFailed("Invalid conversation fields");
        return;
      }
    }
    emit conversationsLoaded(document.array());
  });
}

void ConversationService::createConversation(const QString& jwt,
                                             const QString& name,
                                             const QStringList& participants) {
  QNetworkReply* reply =
      httpClient_.createConversation(jwt, name, participants);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    // Ensure the network reply is released.
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
      emit conversationCreationFailed(reply->errorString());
      return;
    }

    const QByteArray responseBody = reply->readAll();

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(responseBody, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      emit conversationCreationFailed("Invalid conversation response");
      return;
    }

    const QJsonObject json = document.object();

    if (!json.value("id").isString() || !json.value("name").isString()) {
      emit conversationCreationFailed("Missing conversation fields");
      return;
    }

    const QString conversationId = json.value("id").toString();

    const QString conversationName = json.value("name").toString();

    if (conversationId.isEmpty()) {
      emit conversationCreationFailed("Empty conversation ID");
      return;
    }

    // Notify the application that the conversation was created.
    emit conversationCreated(conversationId, conversationName);
  });
}

}  // namespace network