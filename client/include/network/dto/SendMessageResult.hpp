#pragma once

#include <QString>
#include <QMetaType>

namespace network::dto {

enum class SendMessageStatus {
    Success,
    Unauthorized,
    NetworkError,
    ServerError,
    UnknownError
};

struct SendMessageResult {
    SendMessageStatus status;
    int httpStatusCode = 0;
    QString errorMessage;
};

}  // namespace network::dto

Q_DECLARE_METATYPE(network::dto::SendMessageResult)
