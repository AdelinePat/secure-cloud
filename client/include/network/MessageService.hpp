#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "network/HttpClient.hpp"
#include "network/dto/Conversation.hpp"
#include "network/dto/History.hpp"
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

    void listConversations(const QString& jwt);

    void createConversation(
        const QString& jwt,
        const dto::CreateConversationRequest& request
    );

    void loadHistory(
        const QString& jwt,
        const QString& conversationId,
        const QString& before = {},
        int limit = 50
    );

signals:
    void messageSent(
        const QString& messageId,
        const dto::SendMessageResult& result);

    void conversationsLoaded(
        const dto::ConversationsResult& result);

    void conversationCreated(
        const dto::ConversationResult& result);

    void historyLoaded(
        const QString& conversationId,
        const dto::HistoryResult& result);

private:
    HttpClient httpClient_;
};

}  // namespace network
