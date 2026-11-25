#include "network/HttpClient.hpp"

#include <QJsonArray>
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
  QString path = "/messaging/conversations/" + conversationId + "/messages";
  url.setPath(path);

  QNetworkRequest networkRequest(url);

  networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           "application/json");

  networkRequest.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());

  std::string serializedRequest = SendMessageRequestCodec::serialize(request);

  QByteArray body = QByteArray::fromStdString(serializedRequest);

  return networkManager_.post(networkRequest, body);
}

QNetworkReply* HttpClient::getMessageHistory(const QString& conversationId,
                                             const QString& jwt) {
  QUrl url = baseUrl_;
  url.setPath("/messaging/conversations/" + conversationId + "/messages");

  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());

  return networkManager_.get(request);
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

QNetworkReply* HttpClient::listConversations(const QString& jwt) {
  QUrl url = baseUrl_;
  url.setPath("/messaging/conversations");
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
  return networkManager_.get(request);
}

QNetworkReply* HttpClient::createConversation(const QString& jwt,
                                              const QString& name,
                                              const QStringList& participants) {
  QUrl url = baseUrl_;
  url.setPath("/messaging/conversations");

  QNetworkRequest networkRequest(url);

  networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                           "application/json");

  networkRequest.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());

  QJsonArray participantsArray;

  for (const QString& username : participants) {
    const QString trimmedUsername = username.trimmed();

    if (!trimmedUsername.isEmpty()) {
      participantsArray.append(trimmedUsername);
    }
  }

  QJsonObject json{
      {"type", "group"}, {"name", name}, {"participants", participantsArray}};

  const QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);

  return networkManager_.post(networkRequest, body);
}

QNetworkReply* HttpClient::publishIdentity(const QString& jwt, const QString& publicKey) {
  QUrl url = baseUrl_;
  url.setPath("/messaging/identity-key");
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  return networkManager_.put(request, QJsonDocument(QJsonObject{{"public_key", publicKey}}).toJson(QJsonDocument::Compact));
}

QNetworkReply* HttpClient::getPeerKey(const QString& jwt, const QString& conversationId) {
  QUrl url = baseUrl_;
  url.setPath("/messaging/conversations/" + conversationId + "/peer-key");
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
  return networkManager_.get(request);
}

// ── File API ────────────────────────────────────────────────────────────────

namespace {
// A chunk is at most 1 MiB; a stalled transfer fails instead of hanging.
constexpr int kFileTransferTimeoutMs = 60 * 1000;
}  // namespace

QNetworkRequest HttpClient::fileRequest(const QString& jwt,
                                        const QString& path) const {
  QUrl url = baseUrl_;
  url.setPath(path);
  QNetworkRequest request(url);
  request.setRawHeader("Authorization", "Bearer " + jwt.toUtf8());
  request.setTransferTimeout(kFileTransferTimeoutMs);
  return request;
}

QNetworkReply* HttpClient::createFile(const QString& jwt,
                                      const QString& conversationId,
                                      qint64 sizeBytes, qint64 chunkCount) {
  auto request = fileRequest(jwt, "/files");
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  const QJsonObject json{{"conversation_id", conversationId},
                         {"size_bytes", sizeBytes},
                         {"chunk_count", chunkCount}};
  return networkManager_.post(request,
                              QJsonDocument(json).toJson(QJsonDocument::Compact));
}

QNetworkReply* HttpClient::uploadChunk(const QString& jwt, const QString& fileId,
                                       qint64 index,
                                       const QByteArray& ciphertext) {
  auto request = fileRequest(
      jwt, "/files/" + fileId + "/chunks/" + QString::number(index));
  request.setHeader(QNetworkRequest::ContentTypeHeader,
                    "application/octet-stream");
  return networkManager_.put(request, ciphertext);
}

QNetworkReply* HttpClient::completeFile(const QString& jwt,
                                        const QString& fileId) {
  return networkManager_.post(fileRequest(jwt, "/files/" + fileId + "/complete"),
                              QByteArray());
}

QNetworkReply* HttpClient::getFile(const QString& jwt, const QString& fileId) {
  return networkManager_.get(fileRequest(jwt, "/files/" + fileId));
}

QNetworkReply* HttpClient::downloadChunk(const QString& jwt,
                                         const QString& fileId, qint64 index) {
  return networkManager_.get(fileRequest(
      jwt, "/files/" + fileId + "/chunks/" + QString::number(index)));
}

QNetworkReply* HttpClient::deleteFile(const QString& jwt,
                                      const QString& fileId) {
  return networkManager_.deleteResource(fileRequest(jwt, "/files/" + fileId));
}

}  // namespace network
