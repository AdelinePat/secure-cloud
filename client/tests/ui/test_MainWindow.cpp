#include "ui/MainWindow.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QStatusBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>
#include <memory>
#include <sodium.h>

class TestMainWindow : public QObject {
  Q_OBJECT
private slots:
  void loginNavigationAndTokenRotation();
  void listingRejectsInvalidResponses_data();
  void listingRejectsInvalidResponses();
};

void TestMainWindow::loginNavigationAndTokenRotation() {
  QVERIFY(sodium_init() >= 0);
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  QList<QByteArray> requests;
  connect(&server, &QTcpServer::newConnection, &server, [&]() {
    while (server.hasPendingConnections()) {
      auto *socket = server.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer]() {
        buffer->append(socket->readAll());
        const auto split = buffer->indexOf("\r\n\r\n");
        if (split < 0)
          return;
        qsizetype length = 0;
        for (const auto &line : buffer->left(split).split('\n')) {
          if (line.toLower().startsWith("content-length:"))
            length = line.mid(15).trimmed().toLongLong();
        }
        if (buffer->size() < split + 4 + length)
          return;
        const auto request = *buffer;
        buffer->clear();
        requests.append(request);
        QByteArray body;
        if (request.startsWith("POST /auth/login ")) {
          body =
              R"({"access_token":"jwt-first","refresh_token":"refresh-first"})";
        } else if (request.startsWith("POST /auth/refresh ")) {
          body =
              R"({"access_token":"jwt-rotated","refresh_token":"refresh-rotated"})";
        } else if (request.startsWith("GET /messaging/conversations ")) {
          body = R"([{"id":"a","name":"Team"},{"id":"b","name":"Team"}])";
        } else {
          body = R"({"id":"c","name":"New group"})";
        }
        socket->write(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: "
            "close\r\nContent-Length: " +
            QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
      });
    }
  });
  network::HttpClient client(
      QUrl("http://127.0.0.1:" + QString::number(server.serverPort())));
  ui::MainWindow window(client);
  auto *username = window.findChild<QLineEdit *>("usernameInput");
  auto *password = window.findChild<QLineEdit *>("passwordInput");
  auto *list = window.findChild<QListWidget *>("conversationList");
  auto *input = window.findChild<QLineEdit *>("messageInput");
  auto *send = window.findChild<QPushButton *>("sendButton");
  auto *name = window.findChild<QLineEdit *>("conversationNameInput");
  QVERIFY(username && password && list && input && send && name);
  QVERIFY(!send->isEnabled());
  username->setText("test-user");
  password->setText("test-password");
  // Both fields submit; repeated Enter while pending must not duplicate login.
  QTest::keyClick(password, Qt::Key_Return);
  QTest::keyClick(username, Qt::Key_Return);
  QTRY_COMPARE(list->count(), 2);
  QCOMPARE(requests.size(), 2);
  QVERIFY(requests[1].contains("Authorization: Bearer jwt-first"));
  QCOMPARE(list->currentRow(), 0);
  QVERIFY(send->isEnabled());
  input->setText("message A");
  send->click();
  input->setText("draft A");
  list->setCurrentRow(1);
  QVERIFY(input->text().isEmpty());
  auto *view = window.findChild<QTextEdit *>();
  QVERIFY(view->toPlainText().isEmpty());
  input->setText("message B");
  send->click();
  input->setText("draft B");
  list->setCurrentRow(0);
  QCOMPARE(input->text(), QString("draft A"));
  QVERIFY(view->toPlainText().contains("message A"));
  QVERIFY(!view->toPlainText().contains("message B"));
  // A delayed status update for B must not display B in A.
  QTRY_VERIFY_WITH_TIMEOUT(view->toPlainText().contains("[Sent]"), 2500);
  QVERIFY(!view->toPlainText().contains("message B"));
  list->setCurrentRow(1);
  QCOMPARE(input->text(), QString("draft B"));
  QVERIFY(view->toPlainText().contains("message B"));
  QVERIFY(QMetaObject::invokeMethod(&window, "refreshAccessToken"));
  QTRY_COMPARE(requests.size(), 3);
  QTRY_VERIFY(window.findChild<QTimer *>("refreshTimer")->isActive());
  QVERIFY(requests[2].contains("refresh-first"));
  name->setText("New group");
  QTest::keyClick(name, Qt::Key_Return);
  QTRY_COMPARE(list->count(), 3);
  QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), QString("c"));
  QVERIFY(requests.last().contains("Authorization: Bearer jwt-rotated"));
  // The second refresh must send the rotated refresh token.
  QVERIFY(QMetaObject::invokeMethod(&window, "refreshAccessToken"));
  QTRY_COMPARE(requests.size(), 5);
  QVERIFY(requests.last().contains("refresh-rotated"));
}

void TestMainWindow::listingRejectsInvalidResponses_data() {
  QTest::addColumn<QByteArray>("body");
  QTest::addColumn<int>("status");
  QTest::newRow("unauthorized") << QByteArray("[]") << 401;
  QTest::newRow("server-error") << QByteArray("[]") << 500;
  QTest::newRow("invalid-json") << QByteArray("oops") << 200;
  QTest::newRow("object") << QByteArray("{}") << 200;
  QTest::newRow("missing-id") << QByteArray(R"([{"name":"Team"}])") << 200;
  QTest::newRow("empty-id")
      << QByteArray(R"([{"id":"","name":"Team"}])") << 200;
  QTest::newRow("wrong-name-type")
      << QByteArray(R"([{"id":"a","name":42}])") << 200;
}
void TestMainWindow::listingRejectsInvalidResponses() {
  QFETCH(QByteArray, body);
  QFETCH(int, status);
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  connect(&server, &QTcpServer::newConnection, &server, [&]() {
    auto *socket = server.nextPendingConnection();
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::readyRead, socket, [socket, body, status]() {
      socket->readAll();
      socket->write("HTTP/1.1 " + QByteArray::number(status) +
                    " Test\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body);
      socket->disconnectFromHost();
    });
  });
  network::ConversationService service(
      QUrl("http://127.0.0.1:" + QString::number(server.serverPort())));
  QSignalSpy failed(&service,
                    &network::ConversationService::conversationsLoadFailed);
  QSignalSpy loaded(&service,
                    &network::ConversationService::conversationsLoaded);
  service.listConversations("test-jwt");
  QTRY_COMPARE(failed.count(), 1);
  QCOMPARE(loaded.count(), 0);
}
QTEST_MAIN(TestMainWindow)
#include "test_MainWindow.moc"
