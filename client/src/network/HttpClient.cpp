#include "network/HttpClient.hpp"

#include "network/SendMessageRequestCodec.hpp"

namespace network {

HttpClient::HttpClient(const QUrl& baseUrl, QObject* parent)
    : QObject(parent), baseUrl_(baseUrl), networkManager_(this) {}

QNetworkReply* HttpClient::sendMessage(const QString& conversationId,
                                       const QString& jwt,
                                       const dto::SendMessageRequest& request) {
  QUrl url = baseUrl_;
  QString path = "/secureChat/conversations/" + conversationId + "/messages";
  url.setPath(path);

  QNetworkRequest networkRequest(url);

  networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           "application/json");

  networkRequest.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());

  std::string serializedRequest = SendMessageRequestCodec::serialize(request);

  QByteArray body = QByteArray::fromStdString(serializedRequest);

  return networkManager_.post(networkRequest, body);
}

}  // namespace network