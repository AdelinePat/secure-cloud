#pragma once

#include <QNetworkAccessManager>
#include <QObject>

#include "network/dto/SendMessageRequest.hpp"

namespace network {

class HttpClient : public QObject {
  Q_OBJECT

 public:
  explicit HttpClient(const QUrl& baseUrl, QObject* parent = nullptr);

  QNetworkReply* sendMessage(
      const QString& conversationId,
      const QString& jwt,
      const dto::SendMessageRequest& request);

 private:
  QUrl baseUrl_;
  QNetworkAccessManager networkManager_;
};

}  // namespace network