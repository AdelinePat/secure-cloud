#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

#include "network/FileMessage.hpp"
#include "network/HttpClient.hpp"

namespace network {

// Returns the current access token. Called before every request, so a long
// transfer keeps working across token refreshes.
using TokenProvider = std::function<QString()>;

// End-to-end encrypted file transfers over the File API.
//
// Upload: encrypt the file chunk by chunk (one chunk in memory at a time),
// POST /files, PUT each chunk, POST complete, then hand back the FileMessage
// to send in the conversation. Download: fetch and decrypt the chunks in
// order into a temporary file that only replaces the destination once every
// chunk authenticated and the last one was reached.
//
// Network errors and 5xx answers are retried; other errors fail the transfer.
class FileService : public QObject {
  Q_OBJECT

 public:
  static constexpr int kMaxAttempts = 3;
  static constexpr qint64 kMaxFileBytes = 2LL * 1024 * 1024 * 1024;  // ciphertext

  FileService(const QUrl& baseUrl, TokenProvider accessToken,
              QObject* parent = nullptr);
  ~FileService() override;

  // Each returns a transfer id carried by the signals below.
  QString upload(const QString& conversationId, const QString& path,
                 const QString& caption = {});
  QString download(const FileMessage& file, const QString& destinationPath);

  // Stops every transfer without emitting signals (e.g. on logout).
  void cancelAll();

  int activeTransfers() const { return transfers_.size(); }

 signals:
  // Plaintext bytes processed so far.
  void progress(const QString& transferId, qint64 doneBytes, qint64 totalBytes);
  void uploadFinished(const QString& transferId, const QString& conversationId,
                      const network::FileMessage& file);
  void downloadFinished(const QString& transferId, const QString& path);
  void transferFailed(const QString& transferId, const QString& error);

 private:
  class Transfer;
  class Upload;
  class Download;

  void start(Transfer* transfer);
  void finish(const QString& transferId);

  HttpClient http_;
  TokenProvider accessToken_;
  QHash<QString, Transfer*> transfers_;
};

}  // namespace network
