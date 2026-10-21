#include <QHostAddress>

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "network/HttpClient.hpp"
#include "network/dto/SendMessageRequest.hpp"

class TestHttpClient : public QObject {
  Q_OBJECT

 private slots:
  void sendMessage_sendsPostRequest();
  void sendMessage_handlesUnauthorized();
  void sendMessage_handlesConnectionRefused();
};

void TestHttpClient::sendMessage_sendsPostRequest() {
  QTcpServer server;

  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  quint16 port = server.serverPort();
  qDebug() << "Test server port:" << port;

  QUrl baseUrl;
  baseUrl.setScheme("http");
  baseUrl.setHost("127.0.0.1");
  baseUrl.setPort(port);

  network::HttpClient httpClient(baseUrl);

  network::dto::SendMessageRequest request;

  request.messageId = "message-123";
  request.encryptionVersion = 1;
  request.encryptedMessage.nonce.fill(1);
  request.encryptedMessage.ciphertext = {10, 20, 30, 40};
  request.clientTimestamp = "2026-09-25T12:00:00Z";

  QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

  QString conversationId = "conversation-123";
  QString jwt = "fake-jwt-token";

  QNetworkReply* reply = httpClient.sendMessage(conversationId, jwt, request);

  QVERIFY(reply != nullptr);
  QVERIFY(connectionSpy.wait(3000));

  QTcpSocket* socket = server.nextPendingConnection();

  QVERIFY(socket != nullptr);

  QSignalSpy finishedSpy(reply, &QNetworkReply::finished);

  QSignalSpy readyReadSpy(socket, &QTcpSocket::readyRead);

  if (socket->bytesAvailable() == 0) {
    QVERIFY(readyReadSpy.wait(3000));
  }

  QByteArray receivedData = socket->readAll();

  qDebug().noquote() << receivedData;

  QVERIFY(receivedData.startsWith(
      "POST /secureChat/conversations/conversation-123/messages HTTP/1.1"));

  QVERIFY(receivedData.contains("Content-Type: application/json"));

  QVERIFY(receivedData.contains("Authorization: Bearer fake-jwt-token"));

  qsizetype bodyPosition = receivedData.indexOf("\r\n\r\n");

  QVERIFY(bodyPosition != -1);

  QByteArray body = receivedData.mid(bodyPosition + 4);

  QJsonDocument jsonDocument = QJsonDocument::fromJson(body);
  QVERIFY(jsonDocument.isObject());

  QJsonObject json = jsonDocument.object();
  QCOMPARE(json["message_id"].toString(), QString("message-123"));

  QCOMPARE(json["encryption_version"].toInt(), 1);

  QCOMPARE(json["client_timestamp"].toString(),
           QString("2026-09-25T12:00:00Z"));

  QCOMPARE(json["nonce"].toString(),
           QString("AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB"));

  QCOMPARE(json["ciphertext"].toString(), QString("ChQeKA=="));

  QVERIFY(json["attachments"].isArray());
  QVERIFY(json["attachments"].toArray().isEmpty());

  QByteArray responseBody = R"({"success":true})";

  QByteArray response =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " +
    QByteArray::number(responseBody.size()) +
    "\r\n"
    "Connection: close\r\n"
    "\r\n" +
    responseBody;

    qint64 bytesWritten = socket->write(response);

  QCOMPARE(bytesWritten, static_cast<qint64>(response.size()));

  socket->flush();

  if (!reply->isFinished()) {
      QVERIFY(finishedSpy.wait(3000));
  }

  QCOMPARE(
    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
    200
  );

  QByteArray responseData = reply->readAll();

  QJsonDocument responseJson = QJsonDocument::fromJson(responseData);

  QVERIFY(responseJson.isObject());

  QJsonObject responseObject = responseJson.object();

  QVERIFY(responseObject.contains("success"));

  QCOMPARE(responseObject["success"].toBool(), true);

  QVERIFY(responseObject["success"].isBool());

  reply->deleteLater();
}

void TestHttpClient::sendMessage_handlesUnauthorized()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    quint16 port = server.serverPort();

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(port);

    network::HttpClient httpClient(baseUrl);

    network::dto::SendMessageRequest request;

    request.messageId = "message-401";
    request.encryptionVersion = 1;
    request.encryptedMessage.nonce.fill(1);
    request.encryptedMessage.ciphertext = {10, 20, 30, 40};
    request.clientTimestamp = "2026-09-25T12:00:00Z";

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    QNetworkReply* reply = httpClient.sendMessage(
    "conversation-123",
    "invalid-jwt-token",
    request
);

QVERIFY(reply != nullptr);

QSignalSpy finishedSpy(reply, &QNetworkReply::finished);

QVERIFY(connectionSpy.wait(3000));

QTcpSocket* socket = server.nextPendingConnection();

QVERIFY(socket != nullptr);

QSignalSpy readyReadSpy(socket, &QTcpSocket::readyRead);

if (socket->bytesAvailable() == 0) {
    QVERIFY(readyReadSpy.wait(3000));
}

QByteArray receivedData = socket->readAll();

QVERIFY(receivedData.contains(
    "Authorization: Bearer invalid-jwt-token"
));
QByteArray responseBody = R"({"error":"INVALID_TOKEN"})";

QByteArray response =
    "HTTP/1.1 401 Unauthorized\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " +
    QByteArray::number(responseBody.size()) +
    "\r\n"
    "Connection: close\r\n"
    "\r\n" +
    responseBody;

    qint64 bytesWritten = socket->write(response);

QCOMPARE(bytesWritten, static_cast<qint64>(response.size()));

socket->flush();

if (!reply->isFinished()) {
    QVERIFY(finishedSpy.wait(3000));
}

QCOMPARE(
    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
    401
);

QCOMPARE(
    reply->error(),
    QNetworkReply::AuthenticationRequiredError
);

QByteArray responseData = reply->readAll();

QJsonDocument responseJson =
    QJsonDocument::fromJson(responseData);

QVERIFY(responseJson.isObject());

QJsonObject responseObject = responseJson.object();

QVERIFY(responseObject["error"].isString());

QCOMPARE(
    responseObject["error"].toString(),
    QString("INVALID_TOKEN")
);

reply->deleteLater();
}

void TestHttpClient::sendMessage_handlesConnectionRefused()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    quint16 port = server.serverPort();

    server.close();

    QVERIFY(!server.isListening());

    qDebug() << "Closed server port:" << port;
    qDebug() << "Server listening:" << server.isListening();

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(port);

    qDebug() << "Target URL:" << baseUrl.toString();

    network::HttpClient httpClient(baseUrl);

    network::dto::SendMessageRequest request;

    request.messageId = "message-network-error";
    request.encryptionVersion = 1;
    request.encryptedMessage.nonce.fill(1);
    request.encryptedMessage.ciphertext = {10, 20, 30, 40};
    request.clientTimestamp = "2026-09-25T12:00:00Z";

    QNetworkReply* reply = httpClient.sendMessage(
    "conversation-123",
    "fake-jwt-token",
    request
);

QVERIFY(reply != nullptr);

QSignalSpy finishedSpy(reply, &QNetworkReply::finished);

QSignalSpy errorSpy(reply, &QNetworkReply::errorOccurred);

QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000);

qDebug() << "Finished:" << reply->isFinished();
qDebug() << "Error signal count:" << errorSpy.count();
qDebug() << "Error code:" << reply->error();
qDebug() << "Error string:" << reply->errorString();

QCOMPARE(
    reply->error(),
    QNetworkReply::ConnectionRefusedError
);

QVERIFY(
    !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()
);

reply->deleteLater();
}

QTEST_MAIN(TestHttpClient)

#include "test_HttpClient.moc"