#pragma once

#include <QString>
#include <QVector>

#include <QMetaType>

#include "crypto/CryptoTypes.hpp"
#include "network/dto/ApiResult.hpp"

namespace network::dto {

struct MessageAttachment {
    QString fileId;
    qint64 sizeBytes = 0;
    QString checksumSha256;
};

struct ReceivedMessage {
    QString messageId;
    QString userId;
    crypto::EncryptedMessage encryptedMessage;
    int encryptionVersion = 1;
    QString serverTimestamp;
    QVector<MessageAttachment> attachments;
};

struct HistoryPage {
    QVector<ReceivedMessage> messages;
    QString nextCursor;
    bool hasNext = false;
};

struct HistoryResult {
    ApiResult meta;
    HistoryPage page;
};

}  // namespace network::dto

Q_DECLARE_METATYPE(network::dto::HistoryResult)
