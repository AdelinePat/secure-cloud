#include "network/ApiResultMapper.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace network {

namespace {

QString errorMessageFromBody(const QByteArray& body,
                             const QString& fallback) {
  const QJsonDocument document = QJsonDocument::fromJson(body);
  if (!document.isObject()) return fallback;

  const QJsonObject object = document.object();

  if (object.contains("message") && object["message"].isString()) {
    return object["message"].toString();
  }
  if (object.contains("error") && object["error"].isString()) {
    return object["error"].toString();
  }
  if (object.contains("type") && object["type"].isString()) {
    return object["type"].toString();
  }

  return fallback;
}

}  // namespace

dto::ApiResult ApiResultMapper::fromReply(QNetworkReply* reply) {
  return fromReply(reply, reply->readAll());
}

dto::ApiResult ApiResultMapper::fromReply(QNetworkReply* reply,
                                          const QByteArray& body) {
  dto::ApiResult result;
  result.httpStatusCode =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

  const QString fallback = reply->errorString();

  if (reply->error() == QNetworkReply::NoError &&
      result.httpStatusCode >= 200 && result.httpStatusCode < 300) {
    result.status = dto::RequestStatus::Success;
    return result;
  }

  switch (result.httpStatusCode) {
    case 400:
    case 422:
      result.status = dto::RequestStatus::BadRequest;
      break;
    case 401:
      result.status = dto::RequestStatus::Unauthorized;
      break;
    case 403:
      result.status = dto::RequestStatus::Forbidden;
      break;
    case 404:
      result.status = dto::RequestStatus::NotFound;
      break;
    case 409:
      result.status = dto::RequestStatus::Conflict;
      break;
    default:
      if (result.httpStatusCode >= 500 && result.httpStatusCode < 600) {
        result.status = dto::RequestStatus::ServerError;
      } else if (result.httpStatusCode == 0) {
        result.status = dto::RequestStatus::NetworkError;
      } else {
        result.status = dto::RequestStatus::UnknownError;
      }
      break;
  }

  result.errorMessage = errorMessageFromBody(body, fallback);
  if (result.errorMessage.isEmpty()) {
    result.errorMessage = fallback;
  }

  return result;
}

}  // namespace network
