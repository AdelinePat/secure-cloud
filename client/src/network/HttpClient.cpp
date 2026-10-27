#include "network/HttpClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

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

QNetworkReply* HttpClient::login(const QString& username,
                                 const QString& password) {
  QUrl url = baseUrl_;
  url.setPath("/auth/login");

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QJsonObject json{{"username", username}, {"password", password}};

  const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);

  return networkManager_.post(request, body);
}

QNetworkReply* HttpClient::refreshToken(const QString& refreshToken) {
  QUrl url = baseUrl_;
  url.setPath("/auth/refresh");

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

  QJsonObject json{{"refresh_token", refreshToken}};

  const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);

  return networkManager_.post(request, body);
}

QNetworkReply* HttpClient::createConversation(const QString& jwt,
                                              const QString& name) {
  QUrl url = baseUrl_;
  url.setPath("/messaging/conversations");

  QNetworkRequest networkRequest(url);

  networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           "application/json");

  networkRequest.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());

  // Build the conversation creation payload.
  QJsonObject json{{"type", "group"}, {"name", name}};

  const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);

  return networkManager_.post(networkRequest, body);
}

}  // namespace network