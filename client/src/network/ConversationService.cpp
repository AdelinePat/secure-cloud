#include "network/ConversationService.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace network {

ConversationService::ConversationService(const QUrl& baseUrl, QObject* parent)
    : QObject(parent), httpClient_(baseUrl, this) {}

void ConversationService::createConversation(const QString& jwt,
                                             const QString& name) {
  QNetworkReply* reply = httpClient_.createConversation(jwt, name);

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