#include "network/MessageService.hpp"
#include "network/SendMessageResultMapper.hpp"

#include <QNetworkReply>

namespace network {

MessageService::MessageService(const QUrl& baseUrl, QObject* parent)
    : QObject(parent),
      httpClient_(baseUrl, this)
{
}

void MessageService::sendMessage(
    const QString& conversationId,
    const QString& jwt,
    const dto::SendMessageRequest& request)
{
    QNetworkReply* reply =
        httpClient_.sendMessage(conversationId, jwt, request);

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, messageId = QString::fromStdString(request.messageId)]() {

            dto::SendMessageResult result =
                SendMessageResultMapper::fromReply(reply);

            emit messageSent(messageId, result);

            reply->deleteLater();
        }
    );
}

}  // namespace network