#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QUrl>

#include "network/dto/Conversation.hpp"
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

  QNetworkReply* listConversations(const QString& jwt);

  QNetworkReply* createConversation(
      const QString& jwt,
      const dto::CreateConversationRequest& request);

  QNetworkReply* getConversation(const QString& jwt,
                                 const QString& conversationId);

  QNetworkReply* getHistory(const QString& jwt,
                            const QString& conversationId,
                            const QString& before,
                            int limit);

 private:
  QNetworkRequest makeRequest(const QUrl& url, const QString& jwt) const;
  QUrl conversationsUrl(const QString& conversationId = {}) const;

  QUrl baseUrl_;
  QNetworkAccessManager networkManager_;
};

}  // namespace network
