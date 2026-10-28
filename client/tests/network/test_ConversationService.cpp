#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

#include "network/ConversationService.hpp"

class TestConversationService : public QObject {
  Q_OBJECT

 private slots:
  void createConversation_emitsConversationCreated();
  void createConversation_handlesUnauthorized();

  void createConversation_handlesHttpErrors();
  void createConversation_handlesInvalidJson();
  void createConversation_handlesMissingId();
  void createConversation_handlesConnectionRefused();
};

void TestConversationService::createConversation_emitsConversationCreated() {
  QTcpServer server;

  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  QUrl baseUrl;
  baseUrl.setScheme("http");
  baseUrl.setHost("127.0.0.1");
  baseUrl.setPort(server.serverPort());

  network::ConversationService service(baseUrl);

  QSignalSpy createdSpy(&service,
                        &network::ConversationService::conversationCreated);

  QSignalSpy failedSpy(
      &service, &network::ConversationService::conversationCreationFailed);

  QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

  service.createConversation("fake-jwt-token", "Equipe SecureChat");

  QVERIFY(connectionSpy.wait(3000));

  QTcpSocket* socket = server.nextPendingConnection();
  QVERIFY(socket != nullptr);

  QSignalSpy readyReadSpy(socket, &QTcpSocket::readyRead);

  if (socket->bytesAvailable() == 0) {
    QVERIFY(readyReadSpy.wait(3000));
  }

  QByteArray receivedData = socket->readAll();

  QVERIFY(receivedData.startsWith("POST /messaging/conversations HTTP/1.1"));

  QVERIFY(receivedData.contains("Authorization: Bearer fake-jwt-token"));

  QVERIFY(receivedData.contains("Content-Type: application/json"));

  const qsizetype bodyPosition = receivedData.indexOf("\r\n\r\n");
  QVERIFY(bodyPosition != -1);

  const QByteArray requestBody = receivedData.mid(bodyPosition + 4);
  const QJsonDocument requestJson = QJsonDocument::fromJson(requestBody);

  QVERIFY(requestJson.isObject());
  QCOMPARE(requestJson.object().value("type").toString(), QString("group"));
  QCOMPARE(requestJson.object().value("name").toString(),
           QString("Equipe SecureChat"));

  // Simulate a successful response from the Gateway.
  const QByteArray responseBody =
      R"({"id":"2bb0dc14-ecdb-4d4b-882d-2204fa1d6087","name":"Equipe SecureChat","type":"group"})";

  const QByteArray response =
      "HTTP/1.1 201 Created\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      QByteArray::number(responseBody.size()) +
      "\r\n"
      "Connection: close\r\n"
      "\r\n" +
      responseBody;

  QCOMPARE(socket->write(response), static_cast<qint64>(response.size()));

  socket->flush();

  QTRY_COMPARE_WITH_TIMEOUT(createdSpy.count(), 1, 3000);

  QCOMPARE(failedSpy.count(), 0);

  const QList<QVariant> arguments = createdSpy.takeFirst();

  QCOMPARE(arguments.at(0).toString(),
           QString("2bb0dc14-ecdb-4d4b-882d-2204fa1d6087"));

  QCOMPARE(arguments.at(1).toString(), QString("Equipe SecureChat"));
}

void TestConversationService::createConversation_handlesUnauthorized() {
  QTcpServer server;

  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  QUrl baseUrl;
  baseUrl.setScheme("http");
  baseUrl.setHost("127.0.0.1");
  baseUrl.setPort(server.serverPort());

  network::ConversationService service(baseUrl);

  QSignalSpy createdSpy(&service,
                        &network::ConversationService::conversationCreated);

  QSignalSpy failedSpy(
      &service, &network::ConversationService::conversationCreationFailed);

  QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

  service.createConversation("invalid-jwt", "Equipe SecureChat");

  QVERIFY(connectionSpy.wait(3000));

  QTcpSocket* socket = server.nextPendingConnection();
  QVERIFY(socket != nullptr);

  QSignalSpy readyReadSpy(socket, &QTcpSocket::readyRead);

  if (socket->bytesAvailable() == 0) {
    QVERIFY(readyReadSpy.wait(3000));
  }

  const QByteArray requestData = socket->readAll();

  QVERIFY(requestData.contains("Authorization: Bearer invalid-jwt"));

  // Simulate an unauthorized response from Messaging.
  const QByteArray responseBody = R"({"error":"INVALID_TOKEN"})";

  const QByteArray response =
      "HTTP/1.1 401 Unauthorized\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      QByteArray::number(responseBody.size()) +
      "\r\n"
      "Connection: close\r\n"
      "\r\n" +
      responseBody;

  QCOMPARE(socket->write(response), static_cast<qint64>(response.size()));

  socket->flush();

  QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 3000);

  QCOMPARE(createdSpy.count(), 0);

  const QList<QVariant> arguments = failedSpy.takeFirst();

  QVERIFY(!arguments.at(0).toString().isEmpty());
}

void TestConversationService::createConversation_handlesHttpErrors() {
  const QList<int> statusCodes = {400, 500};

  for (int statusCode : statusCodes) {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::ConversationService service(baseUrl);

    QSignalSpy createdSpy(&service,
                          &network::ConversationService::conversationCreated);

    QSignalSpy failedSpy(
        &service, &network::ConversationService::conversationCreationFailed);

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    service.createConversation("test-jwt", "Test");

    QVERIFY(connectionSpy.wait(3000));

    QTcpSocket* socket = server.nextPendingConnection();
    QVERIFY(socket != nullptr);

    const QByteArray body = R"({"error":"REQUEST_FAILED"})";

    const QByteArray response = "HTTP/1.1 " + QByteArray::number(statusCode) +
                                " Error\r\n"
                                "Content-Type: application/json\r\n"
                                "Content-Length: " +
                                QByteArray::number(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body;

    QCOMPARE(socket->write(response), static_cast<qint64>(response.size()));
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 3000);
    QCOMPARE(createdSpy.count(), 0);
  }
}

void TestConversationService::createConversation_handlesInvalidJson() {
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  QUrl baseUrl;
  baseUrl.setScheme("http");
  baseUrl.setHost("127.0.0.1");
  baseUrl.setPort(server.serverPort());

  network::ConversationService service(baseUrl);

  QSignalSpy createdSpy(&service,
                        &network::ConversationService::conversationCreated);

  QSignalSpy failedSpy(
      &service, &network::ConversationService::conversationCreationFailed);

  QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

  service.createConversation("test-jwt", "Test");

  QVERIFY(connectionSpy.wait(3000));

  QTcpSocket* socket = server.nextPendingConnection();
  QVERIFY(socket != nullptr);

  const QByteArray body = "invalid-json";

  const QByteArray response =
      "HTTP/1.1 201 Created\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;

  QCOMPARE(socket->write(response), static_cast<qint64>(response.size()));
  socket->flush();

  QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 3000);
  QCOMPARE(createdSpy.count(), 0);
}

void TestConversationService::createConversation_handlesMissingId() {
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  QUrl baseUrl;
  baseUrl.setScheme("http");
  baseUrl.setHost("127.0.0.1");
  baseUrl.setPort(server.serverPort());

  network::ConversationService service(baseUrl);

  QSignalSpy createdSpy(&service,
                        &network::ConversationService::conversationCreated);

  QSignalSpy failedSpy(
      &service, &network::ConversationService::conversationCreationFailed);

  QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

  service.createConversation("test-jwt", "Test");

  QVERIFY(connectionSpy.wait(3000));

  QTcpSocket* socket = server.nextPendingConnection();
  QVERIFY(socket != nullptr);

  const QByteArray body = R"({"name":"Test","type":"group"})";

  const QByteArray response =
      "HTTP/1.1 201 Created\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: " +
      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;

  QCOMPARE(socket->write(response), static_cast<qint64>(response.size()));
  socket->flush();

  QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 3000);
  QCOMPARE(createdSpy.count(), 0);
}

void TestConversationService::createConversation_handlesConnectionRefused() {
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));

  const quint16 port = server.serverPort();
  server.close();

  QUrl baseUrl;
  baseUrl.setScheme("http");
  baseUrl.setHost("127.0.0.1");
  baseUrl.setPort(port);

  network::ConversationService service(baseUrl);

  QSignalSpy createdSpy(&service,
                        &network::ConversationService::conversationCreated);

  QSignalSpy failedSpy(
      &service, &network::ConversationService::conversationCreationFailed);

  service.createConversation("test-jwt", "Test");

  QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 10000);
  QCOMPARE(createdSpy.count(), 0);
}

QTEST_MAIN(TestConversationService)

#include "test_ConversationService.moc"