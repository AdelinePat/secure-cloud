#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "network/HttpClient.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "network/dto/SendMessageResult.hpp"

namespace network {

class MessageService : public QObject {
    Q_OBJECT

public:
    explicit MessageService(
        const QUrl& baseUrl,
        QObject* parent = nullptr
    );

    void sendMessage(
        const QString& conversationId,
        const QString& jwt,
        const dto::SendMessageRequest& request
    );

signals:
    void messageSent(
        const QString& messageId,
        const dto::SendMessageResult& result);

private:
    HttpClient httpClient_;
};

}  // namespace network