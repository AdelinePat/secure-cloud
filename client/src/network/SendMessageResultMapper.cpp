#include "network/SendMessageResultMapper.hpp"

namespace network {

dto::SendMessageResult SendMessageResultMapper::fromReply(
    QNetworkReply* reply) {

  dto::SendMessageResult result{};

result.status = dto::SendMessageStatus::UnknownError;
result.errorMessage = reply->errorString();
  result.httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  if (reply->error() == QNetworkReply::NoError &&
    result.httpStatusCode >= 200 &&
    result.httpStatusCode < 300) {

    result.status = dto::SendMessageStatus::Success;
    result.errorMessage.clear();
}

if (result.httpStatusCode == 401) {
    result.status = dto::SendMessageStatus::Unauthorized;
    result.errorMessage = reply->errorString();
}

if (reply->error() == QNetworkReply::ConnectionRefusedError) {
    result.status = dto::SendMessageStatus::NetworkError;
    result.errorMessage = reply->errorString();
}

if (result.httpStatusCode >= 500 &&
    result.httpStatusCode < 600) {

    result.status = dto::SendMessageStatus::ServerError;
    result.errorMessage = reply->errorString();
}

  return result;
}

}  // namespace network