#include "network/HttpClient.hpp"

#include <QNetworkRequest>
#include <QUrlQuery>

#include "network/ConversationCodec.hpp"
#include "network/SendMessageRequestCodec.hpp"

namespace network {

HttpClient::HttpClient(const QUrl& baseUrl, QObject* parent)
    : QObject(parent), baseUrl_(baseUrl), networkManager_(this) {}

QUrl HttpClient::conversationsUrl(const QString& conversationId) const {
  QUrl url = baseUrl_;
  QString path = "/secureChat/conversations";

  if (!conversationId.isEmpty()) {
    path += "/" + conversationId;
  }

  url.setPath(path);
  return url;
}

QNetworkRequest HttpClient::makeRequest(const QUrl& url,
                                        const QString& jwt) const {
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
  return request;
}

QNetworkReply* HttpClient::sendMessage(
    const QString& conversationId, const QString& jwt,
    const dto::SendMessageRequest& request) {
  QUrl url = conversationsUrl(conversationId);
  url.setPath(url.path() + "/messages");

  const std::string serialized = SendMessageRequestCodec::serialize(request);
  const QByteArray body = QByteArray::fromStdString(serialized);

  return networkManager_.post(makeRequest(url, jwt), body);
}

QNetworkReply* HttpClient::listConversations(const QString& jwt) {
  return networkManager_.get(makeRequest(conversationsUrl(), jwt));
}

QNetworkReply* HttpClient::createConversation(
    const QString& jwt, const dto::CreateConversationRequest& request) {
  const QByteArray body = ConversationCodec::serializeCreate(request).toUtf8();
  return networkManager_.post(makeRequest(conversationsUrl(), jwt), body);
}

QNetworkReply* HttpClient::getConversation(const QString& jwt,
                                           const QString& conversationId) {
  return networkManager_.get(
      makeRequest(conversationsUrl(conversationId), jwt));
}

QNetworkReply* HttpClient::getHistory(const QString& jwt,
                                      const QString& conversationId,
                                      const QString& before, int limit) {
  QUrl url = conversationsUrl(conversationId);
  url.setPath(url.path() + "/history");

  QUrlQuery query;
  if (!before.isEmpty()) query.addQueryItem("before", before);
  query.addQueryItem("limit", QString::number(limit));
  url.setQuery(query);

  return networkManager_.get(makeRequest(url, jwt));
}

}  // namespace network
