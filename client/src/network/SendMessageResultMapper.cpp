#include "network/SendMessageResultMapper.hpp"

namespace network {

dto::SendMessageResult SendMessageResultMapper::fromReply(
    QNetworkReply* reply) {
  dto::SendMessageResult result{};

  result.status = dto::SendMessageStatus::UnknownError;
  result.httpStatusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  result.errorMessage = reply->errorString();

  // Authentication failure
  if (result.httpStatusCode == 401) {
    result.status = dto::SendMessageStatus::Unauthorized;
    return result;
  }

  // Server-side failure
  if (result.httpStatusCode >= 500) {
    result.status = dto::SendMessageStatus::ServerError;
    return result;
  }

  // Other HTTP failures (403, 404, 409, etc.)
  if (result.httpStatusCode >= 400) {
    result.status = dto::SendMessageStatus::UnknownError;
    return result;
  }

  // Network failure (connection refused, timeout, etc.)
  if (reply->error() != QNetworkReply::NoError) {
    result.status = dto::SendMessageStatus::NetworkError;
    return result;
  }

  // Successful HTTP response
  if (result.httpStatusCode >= 200 && result.httpStatusCode < 300) {
    result.status = dto::SendMessageStatus::Success;
    result.errorMessage.clear();
    return result;
  }

  return result;
}

}  // namespace network