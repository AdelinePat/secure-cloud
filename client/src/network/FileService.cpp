#include "network/FileService.hpp"

#include <sodium.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <exception>
#include <optional>
#include <utility>

#include "crypto/FileCrypto.hpp"

namespace network {

namespace {

QString errorCode(const QByteArray& body) {
  return QJsonDocument::fromJson(body).object().value("error").toString();
}

bool isRetryable(int status) { return status == 0 || status >= 500; }

}  // namespace

// ── Transfer: one upload or download, with request retries ─────────────────

class FileService::Transfer : public QObject {
 public:
  Transfer(FileService& service, QString id)
      : QObject(&service), service_(service), id_(std::move(id)) {}

  ~Transfer() override { cancel(); }

  const QString& id() const { return id_; }
  bool finished() const { return finished_; }
  virtual void start() = 0;

  // Stops silently: no more requests, callbacks or signals.
  void cancel() {
    finished_ = true;
    if (reply_) {
      QNetworkReply* reply = reply_;
      reply_ = nullptr;
      reply->disconnect(this);
      reply->abort();
      reply->deleteLater();
    }
  }

 protected:
  using MakeRequest = std::function<QNetworkReply*(const QString& token)>;
  using OnSuccess = std::function<void(const QByteArray& body)>;

  // Sends a request, retrying network errors and 5xx answers. Any other
  // status than `expected` fails the transfer with a message about `what`.
  void request(const QString& what, MakeRequest make, int expected,
               OnSuccess onSuccess, int attempt = 1) {
    if (finished_) return;  // cancelled while a retry was scheduled
    reply_ = make(service_.accessToken_());
    connect(reply_, &QNetworkReply::finished, this,
            [this, what, make, expected, onSuccess, attempt] {
              QNetworkReply* reply = reply_;
              reply_ = nullptr;
              reply->deleteLater();

              const int status =
                  reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                      .toInt();
              const QByteArray body = reply->readAll();
              if (status == expected) {
                onSuccess(body);
                return;
              }
              if (isRetryable(status) && attempt < kMaxAttempts) {
                QTimer::singleShot(500 * attempt, this,
                                   [this, what, make, expected, onSuccess,
                                    attempt] {
                                     request(what, make, expected, onSuccess,
                                             attempt + 1);
                                   });
                return;
              }
              const QString code = errorCode(body);
              fail(status == 0
                       ? QString("%1: %2").arg(what, reply->errorString())
                       : QString("%1: HTTP %2%3")
                             .arg(what)
                             .arg(status)
                             .arg(code.isEmpty() ? QString() : " " + code));
            });
  }

  void progress(qint64 done, qint64 total) {
    emit service_.progress(id_, done, total);
  }

  // Reports the failure once and ends the transfer.
  void fail(const QString& error) {
    if (finished_) return;
    finished_ = true;
    onFailure();
    emit service_.transferFailed(id_, error);
    service_.finish(id_);
  }

  // Ends the transfer after the subclass emitted its success signal.
  void succeed() {
    finished_ = true;
    service_.finish(id_);
  }

  virtual void onFailure() {}

  FileService& service_;
  QString id_;

 private:
  QPointer<QNetworkReply> reply_;
  bool finished_ = false;
};

// ── Upload ──────────────────────────────────────────────────────────────────

class FileService::Upload : public Transfer {
 public:
  Upload(FileService& service, QString id, QString conversationId,
         QString path, QString caption)
      : Transfer(service, std::move(id)),
        conversationId_(std::move(conversationId)),
        file_(path),
        caption_(std::move(caption)) {}

  void start() override {
    if (!file_.open(QIODevice::ReadOnly)) {
      fail("Cannot read " + file_.fileName() + ": " + file_.errorString());
      return;
    }
    plainSize_ = file_.size();
    chunkCount_ = crypto::fileChunkCount(plainSize_);
    const qint64 cipherSize = crypto::fileCiphertextSize(plainSize_);
    if (cipherSize > kMaxFileBytes) {
      fail("File too large (2 GiB maximum)");
      return;
    }

    request(
        "Creating the upload",
        [this, cipherSize](const QString& token) {
          return service_.http_.createFile(token, conversationId_, cipherSize,
                                           chunkCount_);
        },
        201, [this](const QByteArray& body) {
          const QUuid fileId(
              QJsonDocument::fromJson(body).object().value("file_id").toString());
          if (fileId.isNull()) {
            fail("Creating the upload: invalid server response");
            return;
          }
          fileId_ = fileId.toString(QUuid::WithoutBraces);
          progress(0, plainSize_);
          sendNextChunk();
        });
  }

 private:
  void sendNextChunk() {
    // Encrypt chunk `index_` once; retries resend the same ciphertext (the
    // stream cannot encrypt the same position twice).
    const bool last = index_ == chunkCount_ - 1;
    const QByteArray plain = file_.read(crypto::kFilePlainChunkBytes);
    const qint64 expected =
        last ? plainSize_ - index_ * qint64(crypto::kFilePlainChunkBytes)
             : qint64(crypto::kFilePlainChunkBytes);
    if (plain.size() != expected) {
      fail("The file changed while it was being sent");
      return;
    }
    try {
      const auto cipher = encryptor_.encryptChunk(
          reinterpret_cast<const unsigned char*>(plain.constData()),
          plain.size(), last);
      chunk_ = QByteArray(reinterpret_cast<const char*>(cipher.data()),
                          qsizetype(cipher.size()));
    } catch (const std::exception& e) {
      fail(QString("Encryption failed: %1").arg(e.what()));
      return;
    }
    plainDone_ += plain.size();

    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(digest.data(),
                       reinterpret_cast<const unsigned char*>(chunk_.constData()),
                       chunk_.size());
    const QByteArray localSha =
        QByteArray(reinterpret_cast<const char*>(digest.data()), digest.size())
            .toHex();

    request(
        QString("Uploading chunk %1/%2").arg(index_ + 1).arg(chunkCount_),
        [this](const QString& token) {
          return service_.http_.uploadChunk(token, fileId_, index_, chunk_);
        },
        200, [this, localSha](const QByteArray& body) {
          const auto stored =
              QJsonDocument::fromJson(body).object().value("sha256").toString();
          if (stored.toLatin1() != localSha) {
            fail(QString("Chunk %1 was corrupted in transit").arg(index_ + 1));
            return;
          }
          progress(plainDone_, plainSize_);
          ++index_;
          if (index_ < chunkCount_) {
            sendNextChunk();
          } else {
            completeUpload();
          }
        });
  }

  void completeUpload() {
    chunk_.clear();
    request(
        "Completing the upload",
        [this](const QString& token) {
          return service_.http_.completeFile(token, fileId_);
        },
        200, [this](const QByteArray&) {
          FileMessage message;
          message.fileId = fileId_;
          message.key = encryptor_.key();
          message.header = encryptor_.header();
          message.name = QFileInfo(file_.fileName()).fileName();
          message.size = plainSize_;
          message.mime = QMimeDatabase().mimeTypeForFile(file_.fileName()).name();
          message.text = caption_;
          emit service_.uploadFinished(id_, conversationId_, message);
          sodium_memzero(message.key.data(), message.key.size());
          succeed();
        });
  }

  void onFailure() override {
    // Best effort: do not leave a half-uploaded file on the server.
    if (fileId_.isEmpty()) return;
    QNetworkReply* reply =
        service_.http_.deleteFile(service_.accessToken_(), fileId_);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
  }

  QString conversationId_;
  QFile file_;
  QString caption_;
  crypto::FileEncryptor encryptor_;
  QString fileId_;
  qint64 plainSize_ = 0;
  qint64 plainDone_ = 0;
  qint64 chunkCount_ = 0;
  qint64 index_ = 0;
  QByteArray chunk_;
};

// ── Download ────────────────────────────────────────────────────────────────

class FileService::Download : public Transfer {
 public:
  Download(FileService& service, QString id, const FileMessage& file,
           const QString& destination)
      : Transfer(service, std::move(id)), file_(file), output_(destination) {}

  ~Download() override {
    sodium_memzero(file_.key.data(), file_.key.size());
  }

  void start() override {
    // Writes go to a temporary file; the destination is only replaced by
    // commit(), after the whole file authenticated.
    if (!output_.open(QIODevice::WriteOnly)) {
      fail("Cannot write " + output_.fileName() + ": " + output_.errorString());
      return;
    }
    try {
      decryptor_.emplace(file_.key, file_.header);
    } catch (const std::exception& e) {
      fail(e.what());
      return;
    }

    request(
        "Reading the file information",
        [this](const QString& token) {
          return service_.http_.getFile(token, file_.fileId);
        },
        200, [this](const QByteArray& body) {
          const auto json = QJsonDocument::fromJson(body).object();
          if (json.value("status").toString() != "AVAILABLE") {
            fail("The file is not available yet");
            return;
          }
          chunkCount_ = json.value("chunk_count").toInteger();
          // The sender's message and the server must describe the same file.
          if (chunkCount_ != crypto::fileChunkCount(file_.size) ||
              json.value("size_bytes").toInteger() !=
                  crypto::fileCiphertextSize(file_.size)) {
            fail("The stored file does not match the message");
            return;
          }
          progress(0, file_.size);
          fetchNextChunk();
        });
  }

 private:
  void fetchNextChunk() {
    request(
        QString("Downloading chunk %1/%2").arg(index_ + 1).arg(chunkCount_),
        [this](const QString& token) {
          return service_.http_.downloadChunk(token, file_.fileId, index_);
        },
        200, [this](const QByteArray& body) {
          crypto::Bytes plain;
          try {
            plain = decryptor_->decryptChunk(
                reinterpret_cast<const unsigned char*>(body.constData()),
                body.size());
          } catch (const std::exception&) {
            fail("The file was modified or corrupted: download discarded");
            return;
          }
          const bool last = index_ == chunkCount_ - 1;
          if (decryptor_->finished() != last) {
            fail("The file was truncated or reordered: download discarded");
            return;
          }
          if (output_.write(reinterpret_cast<const char*>(plain.data()),
                            qint64(plain.size())) != qint64(plain.size())) {
            fail("Cannot write " + output_.fileName() + ": " +
                 output_.errorString());
            return;
          }
          sodium_memzero(plain.data(), plain.size());
          done_ += qint64(plain.size());
          progress(done_, file_.size);

          if (!last) {
            ++index_;
            fetchNextChunk();
          } else if (!output_.commit()) {
            fail("Cannot save " + output_.fileName() + ": " +
                 output_.errorString());
          } else {
            emit service_.downloadFinished(id_, output_.fileName());
            succeed();
          }
        });
  }

  void onFailure() override { output_.cancelWriting(); }

  FileMessage file_;
  QSaveFile output_;
  std::optional<crypto::FileDecryptor> decryptor_;
  qint64 chunkCount_ = 0;
  qint64 index_ = 0;
  qint64 done_ = 0;
};

// ── FileService ─────────────────────────────────────────────────────────────

FileService::FileService(const QUrl& baseUrl, TokenProvider accessToken,
                         QObject* parent)
    : QObject(parent), http_(baseUrl, this), accessToken_(std::move(accessToken)) {}

FileService::~FileService() { cancelAll(); }

QString FileService::upload(const QString& conversationId, const QString& path,
                            const QString& caption) {
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  start(new Upload(*this, id, conversationId, path, caption));
  return id;
}

QString FileService::download(const FileMessage& file,
                              const QString& destinationPath) {
  const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  start(new Download(*this, id, file, destinationPath));
  return id;
}

void FileService::start(Transfer* transfer) {
  transfers_.insert(transfer->id(), transfer);
  // Start on the next event loop turn so callers can store the id before
  // the first signal (an unreadable file fails immediately).
  QTimer::singleShot(0, transfer, [transfer] {
    if (!transfer->finished()) transfer->start();
  });
}

void FileService::finish(const QString& transferId) {
  if (Transfer* transfer = transfers_.take(transferId)) transfer->deleteLater();
}

void FileService::cancelAll() {
  // deleteLater: cancelAll may be called from a slot connected to one of
  // these transfers' signals, i.e. while that transfer is still running.
  const auto transfers = std::exchange(transfers_, {});
  for (Transfer* transfer : transfers) {
    transfer->cancel();
    transfer->deleteLater();
  }
}

}  // namespace network
