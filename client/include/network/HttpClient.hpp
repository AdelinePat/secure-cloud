#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QUrl>

#include "network/dto/SendMessageRequest.hpp"

namespace network {

class HttpClient : public QObject {
  Q_OBJECT

 public:
  explicit HttpClient(const QUrl& baseUrl, QObject* parent = nullptr);
  QNetworkReply* login(const QString& username, const QString& password);

  QNetworkReply* refreshToken(const QString& refreshToken);

  QNetworkReply* sendMessage(const QString& conversationId, const QString& jwt,
                             const dto::SendMessageRequest& request);

 private:
  QUrl baseUrl_;
  QNetworkAccessManager networkManager_;
};

}  // namespace network