#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <QMetaType>

#include "network/dto/ApiResult.hpp"

namespace network::dto {

struct ConversationSummary {
    QString id;
    QString type;
    QString name;  // empty for a direct conversation
    QStringList participantIds;
    QString lastMessageAt;
    int unreadCount = 0;
};

struct ConversationParticipant {
    QString userId;
    QString role;
    QString joinedAt;
};

struct ConversationDetails {
    QString id;
    QString type;
    QString name;  // empty for a direct conversation
    QString createdAt;
    QString creatorId;
    QVector<ConversationParticipant> participants;
};

struct CreateConversationRequest {
    QString type;  // "direct" | "group"
    QStringList participantIds;
    QString name;  // required for groups
};

struct ConversationResult {
    ApiResult meta;
    ConversationDetails conversation;
};

struct ConversationsResult {
    ApiResult meta;
    QVector<ConversationSummary> conversations;
};

}  // namespace network::dto

Q_DECLARE_METATYPE(network::dto::ConversationResult)
Q_DECLARE_METATYPE(network::dto::ConversationsResult)
