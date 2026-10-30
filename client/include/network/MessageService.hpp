#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "network/HttpClient.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "network/dto/SendMessageResult.hpp"

namespace network {

class MessageService : public QObject {
  Q_OBJECT

 public:
  explicit MessageService(const QUrl& baseUrl, QObject* parent = nullptr);

  void sendMessage(const QString& conversationId, const QString& jwt,
                   const dto::SendMessageRequest& request);

  void getMessageHistory(const QString& conversationId, const QString& jwt);

 signals:
  void messageSent(const QString& messageId,
                   const dto::SendMessageResult& result);

  void messageHistoryReceived(const QString& conversationId,
                              const QByteArray& response, int httpStatus);

 private:
  HttpClient httpClient_;
};

}  // namespace network