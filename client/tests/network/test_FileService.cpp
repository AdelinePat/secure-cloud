#include <sodium.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest/QtTest>
#include <functional>
#include <map>
#include <memory>

#include "crypto/FileCrypto.hpp"
#include "network/FileService.hpp"

// In-memory stand-in for the file service (same routes, statuses and checks
// as file/src/http/FileRouter.cpp), with hooks to inject faults.
class FakeFileServer : public QObject {
 public:
  struct StoredFile {
    QString conversationId;
    qint64 sizeBytes = 0;
    qint64 chunkCount = 0;
    QString status = "UPLOADING";
    std::map<qint64, QByteArray> chunks;
  };

  struct Request {
    QByteArray method;
    QString path;
    QByteArray authorization;
    QByteArray body;
  };

  // Return true to answer the request yourself via `status`/`body`.
  using Hook = std::function<bool(const Request&, int& status, QByteArray& body)>;

  static constexpr const char* kToken = "test-token";
  static constexpr const char* kConversation = "22222222-2222-4222-8222-222222222222";

  FakeFileServer() {
    QVERIFY(server_.listen(QHostAddress::LocalHost, 0));
    connect(&server_, &QTcpServer::newConnection, this, [this] {
      while (server_.hasPendingConnections()) accept(server_.nextPendingConnection());
    });
  }

  QUrl url() const {
    return QUrl(QString("http://127.0.0.1:%1").arg(server_.serverPort()));
  }

  std::map<QString, StoredFile> files;
  QList<Request> log;
  Hook hook;

 private:
  void accept(QTcpSocket* socket) {
    struct Pending {
      QByteArray buffer;
      qsizetype bodyStart = -1;  // set once the headers are parsed
      qsizetype length = 0;
      Request request;
    };
    auto pending = std::make_shared<Pending>();
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::readyRead, socket, [this, socket, pending] {
      pending->buffer.append(socket->readAll());
      Request& request = pending->request;
      if (pending->bodyStart < 0) {
        const auto split = pending->buffer.indexOf("\r\n\r\n");
        if (split < 0) return;
        pending->bodyStart = split + 4;
        const auto lines = pending->buffer.left(split).split('\n');
        const auto requestLine = lines.first().trimmed().split(' ');
        request.method = requestLine.value(0);
        request.path = QString::fromLatin1(requestLine.value(1));
        for (const auto& line : lines) {
          const auto lower = line.toLower();
          if (lower.startsWith("content-length:"))
            pending->length = line.mid(15).trimmed().toLongLong();
          if (lower.startsWith("authorization:"))
            request.authorization = line.mid(14).trimmed();
        }
      }
      if (pending->buffer.size() < pending->bodyStart + pending->length) return;
      request.body = pending->buffer.mid(pending->bodyStart, pending->length);
      log.append(request);

      int status = 500;
      QByteArray body;
      if (!hook || !hook(request, status, body)) handle(request, status, body);

      socket->write("HTTP/1.1 " + QByteArray::number(status) +
                    " X\r\nContent-Type: application/json\r\nConnection: close\r\n"
                    "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" +
                    body);
      socket->disconnectFromHost();
    });
  }

  static QByteArray error(const char* code) {
    return QByteArray(R"({"error":")") + code + R"("})";
  }

  QByteArray fileJson(const QString& id, const StoredFile& file) const {
    QJsonObject json{{"file_id", id},
                     {"conversation_id", file.conversationId},
                     {"status", file.status},
                     {"size_bytes", file.sizeBytes},
                     {"chunk_count", file.chunkCount}};
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
  }

  void handle(const Request& request, int& status, QByteArray& body) {
    if (request.authorization != QByteArray("Bearer ") + kToken) {
      status = 401;
      body = error("UNAUTHORIZED");
      return;
    }
    const auto parts = request.path.split('/', Qt::SkipEmptyParts);
    if (request.method == "POST" && parts == QStringList{"files"}) {
      const auto json = QJsonDocument::fromJson(request.body).object();
      if (json.value("conversation_id").toString() != kConversation) {
        status = 403;
        body = error("FORBIDDEN");
        return;
      }
      const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      StoredFile file;
      file.conversationId = kConversation;
      file.sizeBytes = json.value("size_bytes").toInteger();
      file.chunkCount = json.value("chunk_count").toInteger();
      files[id] = file;
      status = 201;
      body = fileJson(id, file);
      return;
    }
    if (parts.size() < 2 || parts[0] != "files" || !files.count(parts[1])) {
      status = 404;
      body = error("FILE_NOT_FOUND");
      return;
    }
    StoredFile& file = files[parts[1]];
    if (parts.size() == 2 && request.method == "GET") {
      status = 200;
      body = fileJson(parts[1], file);
    } else if (parts.size() == 2 && request.method == "DELETE") {
      files.erase(parts[1]);
      status = 204;
    } else if (parts.size() == 3 && parts[2] == "complete" && request.method == "POST") {
      qint64 total = 0;
      for (const auto& [index, chunk] : file.chunks) total += chunk.size();
      if (qint64(file.chunks.size()) != file.chunkCount || total != file.sizeBytes) {
        status = 409;
        body = error("INCOMPLETE_UPLOAD");
        return;
      }
      file.status = "AVAILABLE";
      status = 200;
      body = fileJson(parts[1], file);
    } else if (parts.size() == 4 && parts[2] == "chunks") {
      const qint64 index = parts[3].toLongLong();
      if (index < 0 || index >= file.chunkCount) {
        status = 400;
        body = error("INVALID_CHUNK_INDEX");
      } else if (request.method == "PUT") {
        if (file.status != "UPLOADING") {
          status = 409;
          body = error("FILE_NOT_UPLOADING");
          return;
        }
        file.chunks[index] = request.body;
        std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
        crypto_hash_sha256(digest.data(),
                           reinterpret_cast<const unsigned char*>(request.body.constData()),
                           request.body.size());
        status = 200;
        body = QJsonDocument(QJsonObject{
                                 {"chunk_index", index},
                                 {"size_bytes", request.body.size()},
                                 {"sha256", QString::fromLatin1(
                                                QByteArray(reinterpret_cast<const char*>(digest.data()),
                                                           digest.size())
                                                    .toHex())}})
                   .toJson(QJsonDocument::Compact);
      } else if (file.status != "AVAILABLE") {
        status = 409;
        body = error("FILE_NOT_AVAILABLE");
      } else {
        status = 200;
        body = file.chunks[index];
      }
    } else {
      status = 405;
      body = error("METHOD_NOT_ALLOWED");
    }
  }

  QTcpServer server_;
};

namespace {

constexpr qint64 kChunk = crypto::kFilePlainChunkBytes;
constexpr int kTimeoutMs = 20000;

QByteArray randomBytes(qint64 size) {
  QByteArray bytes(size, Qt::Uninitialized);
  randombytes_buf(bytes.data(), size_t(bytes.size()));
  return bytes;
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString& path, const QByteArray& data) {
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QCOMPARE(file.write(data), data.size());
}

int countRequests(const FakeFileServer& server, const QByteArray& method,
                  const QString& pathPart) {
  return int(std::count_if(server.log.begin(), server.log.end(), [&](const auto& r) {
    return r.method == method && r.path.contains(pathPart);
  }));
}

// Runs one upload to completion; returns the FileMessage or fails the test.
std::optional<network::FileMessage> uploadFile(network::FileService& service,
                                               const QString& path,
                                               const QString& conversation,
                                               QString* error = nullptr) {
  QSignalSpy finished(&service, &network::FileService::uploadFinished);
  QSignalSpy failed(&service, &network::FileService::transferFailed);
  service.upload(conversation, path, "caption");
  if (!QTest::qWaitFor([&] { return finished.count() + failed.count() > 0; }, kTimeoutMs))
    return std::nullopt;
  if (!failed.isEmpty()) {
    if (error) *error = failed.first().at(1).toString();
    return std::nullopt;
  }
  return finished.first().at(2).value<network::FileMessage>();
}

// Runs one download; returns the error, or an empty string on success.
QString downloadFile(network::FileService& service,
                     const network::FileMessage& message, const QString& path) {
  QSignalSpy finished(&service, &network::FileService::downloadFinished);
  QSignalSpy failed(&service, &network::FileService::transferFailed);
  service.download(message, path);
  if (!QTest::qWaitFor([&] { return finished.count() + failed.count() > 0; }, kTimeoutMs))
    return "timeout";
  return failed.isEmpty() ? QString() : failed.first().at(1).toString();
}

}  // namespace

class TestFileService : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() {
    QVERIFY(sodium_init() >= 0);
    QVERIFY(dir_.isValid());
  }

  void roundTrip_data() {
    QTest::addColumn<qint64>("size");
    QTest::newRow("empty") << qint64(0);
    QTest::newRow("small") << qint64(5);
    QTest::newRow("exactly one chunk") << kChunk;
    QTest::newRow("several chunks") << 2 * kChunk + kChunk / 2;
  }

  void roundTrip() {
    QFETCH(qint64, size);
    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });

    const QByteArray plain = randomBytes(size);
    const QString source = dir_.filePath("report.pdf");
    writeFile(source, plain);

    const auto message = uploadFile(service, source, FakeFileServer::kConversation);
    QVERIFY(message.has_value());
    QCOMPARE(message->name, "report.pdf");
    QCOMPARE(message->size, size);
    QCOMPARE(message->mime, "application/pdf");
    QCOMPARE(message->text, "caption");

    // The server holds a complete file of ciphertext only.
    QCOMPARE(server.files.size(), std::size_t(1));
    const auto& stored = server.files.at(message->fileId);
    QCOMPARE(stored.status, "AVAILABLE");
    QCOMPARE(stored.chunkCount, crypto::fileChunkCount(size));
    QCOMPARE(stored.sizeBytes, crypto::fileCiphertextSize(size));
    if (size > 0) QVERIFY(!stored.chunks.at(0).contains(plain.left(64)));

    const QString destination = dir_.filePath("downloaded.pdf");
    QCOMPARE(downloadFile(service, *message, destination), QString());
    QCOMPARE(readFile(destination), plain);
    QCOMPARE(service.activeTransfers(), 0);
  }

  void reportsProgressInPlaintextBytes() {
    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    const QString source = dir_.filePath("progress.bin");
    writeFile(source, randomBytes(2 * kChunk + 10));

    QSignalSpy progress(&service, &network::FileService::progress);
    QVERIFY(uploadFile(service, source, FakeFileServer::kConversation).has_value());

    QList<qint64> done;
    for (const auto& args : progress) {
      QCOMPARE(args.at(2).toLongLong(), 2 * kChunk + 10);
      done.append(args.at(1).toLongLong());
    }
    QCOMPARE(done, (QList<qint64>{0, kChunk, 2 * kChunk, 2 * kChunk + 10}));
  }

  void retriesTransientErrors() {
    FakeFileServer server;
    int failuresLeft = 2;  // fewer than kMaxAttempts
    server.hook = [&](const FakeFileServer::Request& r, int& status, QByteArray& body) {
      if (r.method == "PUT" && r.path.endsWith("/chunks/1") && failuresLeft > 0) {
        --failuresLeft;
        status = 502;
        body = R"({"error":"FILE_SERVICE_UNAVAILABLE"})";
        return true;
      }
      return false;
    };
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    const QString source = dir_.filePath("retry.bin");
    const QByteArray plain = randomBytes(2 * kChunk);
    writeFile(source, plain);

    const auto message = uploadFile(service, source, FakeFileServer::kConversation);
    QVERIFY(message.has_value());
    QCOMPARE(countRequests(server, "PUT", "/chunks/1"), 3);

    const QString destination = dir_.filePath("retry.out");
    QCOMPARE(downloadFile(service, *message, destination), QString());
    QCOMPARE(readFile(destination), plain);
  }

  void usesTheCurrentTokenForEveryRequest() {
    // A refresh during a long upload must not break it.
    FakeFileServer server;
    QString token = "stale-token";
    server.hook = [&](const FakeFileServer::Request&, int&, QByteArray&) {
      token = FakeFileServer::kToken;  // refreshed after the first request
      return false;
    };
    network::FileService service(server.url(), [&] { return token; });
    const QString source = dir_.filePath("token.bin");
    writeFile(source, randomBytes(10));

    QString error;
    QVERIFY(!uploadFile(service, source, FakeFileServer::kConversation, &error));
    QCOMPARE(error, "Creating the upload: HTTP 401 UNAUTHORIZED");
    QVERIFY(uploadFile(service, source, FakeFileServer::kConversation).has_value());
  }

  void clientErrorsFailWithoutRetry() {
    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    const QString source = dir_.filePath("forbidden.bin");
    writeFile(source, randomBytes(10));

    QString error;
    QVERIFY(!uploadFile(service, source, "33333333-3333-4333-8333-333333333333", &error));
    QCOMPARE(error, "Creating the upload: HTTP 403 FORBIDDEN");
    QCOMPARE(countRequests(server, "POST", "/files"), 1);
  }

  void failedUploadIsDeletedFromTheServer() {
    FakeFileServer server;
    server.hook = [](const FakeFileServer::Request& r, int& status, QByteArray& body) {
      if (r.method != "PUT") return false;
      status = 409;
      body = R"({"error":"FILE_NOT_UPLOADING"})";
      return true;
    };
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    const QString source = dir_.filePath("rejected.bin");
    writeFile(source, randomBytes(10));

    QString error;
    QVERIFY(!uploadFile(service, source, FakeFileServer::kConversation, &error));
    QCOMPARE(error, "Uploading chunk 1/1: HTTP 409 FILE_NOT_UPLOADING");
    QTRY_VERIFY_WITH_TIMEOUT(server.files.empty(), kTimeoutMs);
    QCOMPARE(countRequests(server, "DELETE", "/files/"), 1);
  }

  void unreadableFileFails() {
    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    QString error;
    QVERIFY(!uploadFile(service, dir_.filePath("missing.bin"),
                        FakeFileServer::kConversation, &error));
    QVERIFY(error.startsWith("Cannot read"));
    QVERIFY(server.log.isEmpty());
  }

  void tamperedDownloadNeverReachesTheDestination_data() {
    QTest::addColumn<QString>("attack");
    QTest::addColumn<QString>("expectedError");
    QTest::newRow("modified chunk") << "modify"
                                    << "The file was modified or corrupted: download discarded";
    QTest::newRow("swapped chunks") << "swap"
                                    << "The file was modified or corrupted: download discarded";
    QTest::newRow("wrong key") << "key"
                               << "The file was modified or corrupted: download discarded";
    QTest::newRow("size differs from message") << "size"
                                               << "The stored file does not match the message";
  }

  void tamperedDownloadNeverReachesTheDestination() {
    QFETCH(QString, attack);
    QFETCH(QString, expectedError);

    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    const QString source = dir_.filePath("original.bin");
    writeFile(source, randomBytes(2 * kChunk + 10));
    auto message = uploadFile(service, source, FakeFileServer::kConversation);
    QVERIFY(message.has_value());

    auto& chunks = server.files.at(message->fileId).chunks;
    if (attack == "modify") chunks[1][5] = char(chunks[1][5] ^ 1);
    if (attack == "swap") std::swap(chunks[0], chunks[1]);
    if (attack == "key") message->key[0] ^= 1;
    if (attack == "size") message->size += 1;

    // An existing file at the destination is left untouched.
    const QString destination = dir_.filePath("tampered.out");
    writeFile(destination, "previous content");
    QCOMPARE(downloadFile(service, *message, destination), expectedError);
    QCOMPARE(readFile(destination), QByteArray("previous content"));
    QCOMPARE(QDir(dir_.path()).entryList({"tampered.out*"}, QDir::Files),
             QStringList{"tampered.out"});  // no leftover temporary file
  }

  void downloadOfMissingFileFails() {
    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    network::FileMessage message;
    message.fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    message.size = 1;
    const QString destination = dir_.filePath("missing.out");
    QCOMPARE(downloadFile(service, message, destination),
             "Reading the file information: HTTP 404 FILE_NOT_FOUND");
    QVERIFY(!QFile::exists(destination));
  }

  void cancelAllStopsSilently() {
    FakeFileServer server;
    network::FileService service(server.url(), [] { return QString(FakeFileServer::kToken); });
    const QString source = dir_.filePath("cancel.bin");
    writeFile(source, randomBytes(3 * kChunk));

    QSignalSpy finished(&service, &network::FileService::uploadFinished);
    QSignalSpy failed(&service, &network::FileService::transferFailed);
    service.upload(FakeFileServer::kConversation, source);
    QTRY_VERIFY_WITH_TIMEOUT(countRequests(server, "PUT", "/chunks/") > 0, kTimeoutMs);
    service.cancelAll();
    QCOMPARE(service.activeTransfers(), 0);

    QTest::qWait(300);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(failed.count(), 0);
  }

  // Optional: against the real stack (gateway + file service). Set
  // SECURECHAT_E2E_URL (e.g. http://localhost:8080), SECURECHAT_E2E_TOKEN
  // (an access token) and SECURECHAT_E2E_CONVERSATION (a conversation the
  // token's user belongs to). With SECURECHAT_E2E_PEER_TOKEN (another
  // participant), that user downloads the file.
  void realServerRoundTrip() {
    const auto url = qEnvironmentVariable("SECURECHAT_E2E_URL");
    const auto token = qEnvironmentVariable("SECURECHAT_E2E_TOKEN");
    const auto conversation = qEnvironmentVariable("SECURECHAT_E2E_CONVERSATION");
    if (url.isEmpty() || token.isEmpty() || conversation.isEmpty())
      QSKIP("SECURECHAT_E2E_* not set");

    network::FileService service(QUrl(url), [&] { return token; });
    const QByteArray plain = randomBytes(2 * kChunk + 12345);
    const QString source = dir_.filePath("e2e.bin");
    writeFile(source, plain);

    QString error;
    const auto message = uploadFile(service, source, conversation, &error);
    QVERIFY2(message.has_value(), qPrintable(error));
    const auto peerToken = qEnvironmentVariable("SECURECHAT_E2E_PEER_TOKEN");
    network::FileService peerService(
        QUrl(url), [&] { return peerToken.isEmpty() ? token : peerToken; });
    const QString destination = dir_.filePath("e2e.out");
    QCOMPARE(downloadFile(peerService, *message, destination), QString());
    QCOMPARE(readFile(destination), plain);
    qInfo() << "uploaded and downloaded file" << message->fileId;
  }

 private:
  QTemporaryDir dir_;
};

QTEST_GUILESS_MAIN(TestFileService)
#include "test_FileService.moc"
