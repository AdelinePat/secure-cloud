#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

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
}

QTEST_MAIN(TestHttpClient)

#include "test_HttpClient.moc"