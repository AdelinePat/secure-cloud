#pragma once

#include <QMetaType>
#include <QString>

namespace network::dto {

enum class RequestStatus {
    Success,
    Unauthorized,
    Forbidden,
    BadRequest,
    NotFound,
    Conflict,
    NetworkError,
    ServerError,
    UnknownError
};

// Kept as a separate enum in dto/SendMessageResult.hpp for the existing
// send-message API. ApiResult is used by the conversation/history calls.

struct ApiResult {
    RequestStatus status = RequestStatus::UnknownError;
    int httpStatusCode = 0;
    QString errorMessage;
};

}  // namespace network::dto

Q_DECLARE_METATYPE(network::dto::ApiResult)
