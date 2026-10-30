#include "network/MessageService.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>

#include "network/SendMessageResultMapper.hpp"

namespace network {

MessageService::MessageService(const QUrl& baseUrl, QObject* parent)
    : QObject(parent), httpClient_(baseUrl, this) {}

void MessageService::sendMessage(const QString& conversationId,
                                 const QString& jwt,
                                 const dto::SendMessageRequest& request) {
  QNetworkReply* reply = httpClient_.sendMessage(conversationId, jwt, request);

  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, messageId = QString::fromStdString(request.messageId)]() {
        dto::SendMessageResult result =
            SendMessageResultMapper::fromReply(reply);

        emit messageSent(messageId, result);

        reply->deleteLater();
      });
}

void MessageService::getMessageHistory(const QString& conversationId,
                                       const QString& jwt) {
  QNetworkReply* reply = httpClient_.getMessageHistory(conversationId, jwt);

  connect(
      reply, &QNetworkReply::finished, this, [this, reply, conversationId]() {
        const QByteArray response = reply->readAll();

        const int httpStatus =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        emit messageHistoryReceived(conversationId, response, httpStatus);

        reply->deleteLater();
      });
}

}  // namespace network