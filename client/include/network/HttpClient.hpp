#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QStringList>
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

  // Retrieve encrypted messages from a conversation.
  QNetworkReply* getMessageHistory(const QString& conversationId,
                                   const QString& jwt);

  QNetworkReply* publishIdentity(const QString& jwt, const QString& publicKey);
  QNetworkReply* getPeerKey(const QString& jwt, const QString& conversationId);
  QNetworkReply* getPeerKeys(const QString& jwt, const QString& conversationId);
  QNetworkReply* listConversations(const QString& jwt);
  const QUrl& baseUrl() const { return baseUrl_; }

  // Create a group conversation through the Gateway.
  QNetworkReply* createConversation(const QString& jwt, const QString& name,
                                    const QStringList& participants);

  // File API (see _Docs/API/File_API.md). Bodies are ciphertext only.
  QNetworkReply* createFile(const QString& jwt, const QString& conversationId,
                            qint64 sizeBytes, qint64 chunkCount);
  QNetworkReply* uploadChunk(const QString& jwt, const QString& fileId,
                             qint64 index, const QByteArray& ciphertext);
  QNetworkReply* completeFile(const QString& jwt, const QString& fileId);
  QNetworkReply* getFile(const QString& jwt, const QString& fileId);
  QNetworkReply* downloadChunk(const QString& jwt, const QString& fileId,
                               qint64 index);
  QNetworkReply* deleteFile(const QString& jwt, const QString& fileId);

 private:
  QNetworkRequest fileRequest(const QString& jwt, const QString& path) const;

  QUrl baseUrl_;
  QNetworkAccessManager networkManager_;
};

}  // namespace network