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
}

QTEST_MAIN(TestHttpClient)

#include "test_HttpClient.moc"