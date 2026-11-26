#include <sodium.h>

#include <QDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QInputDialog>
#include <QApplication>
#include <QStandardPaths>
#include <QUuid>
#include "crypto/PeerTrustStore.hpp"
#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"
#include <QJsonObject>
#include <QStatusBar>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>
#include <algorithm>
#include <memory>

#include "ui/MainWindow.hpp"

class TestMainWindow : public QObject {
  Q_OBJECT
 private slots:
  void securityDialogOpensAndCloses();
  void loginNavigationAndTokenRotation();
  void listingRejectsInvalidResponses_data();
  void listingRejectsInvalidResponses();
};

void TestMainWindow::securityDialogOpensAndCloses() {
  network::HttpClient client(QUrl("http://127.0.0.1"));
  ui::MainWindow window(client);

  auto* button = window.findChild<QPushButton*>("securityButton");
  auto* dialog = window.findChild<QDialog*>("securityDialog");
  QVERIFY(button && dialog);
  QVERIFY(!dialog->isVisible());

  button->click();
  QVERIFY(dialog->isVisible());
  QVERIFY(dialog->findChild<QLabel*>("localFingerprintLabel"));
  QVERIFY(dialog->findChild<QLabel*>("peerTrustLabel"));

  QTest::keyClick(dialog, Qt::Key_Escape);
  QVERIFY(!dialog->isVisible());
}

void TestMainWindow::loginNavigationAndTokenRotation() {
  QVERIFY(sodium_init() >= 0);
  QStandardPaths::setTestModeEnabled(true);
  const auto peer = crypto::KeyManager::generateKeyPair();
  const auto replacement = crypto::KeyManager::generateKeyPair();
  const auto localId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const QString peerId = "550e8400-e29b-41d4-a716-446655440001";
  const QString otherPeerId = "550e8400-e29b-41d4-a716-446655440002";
  auto offeredPeer = peerId;
  auto offeredKey = peer.publicKey;
  bool passphrasePromptShown = false;
  QTimer dialogTimer;
  connect(&dialogTimer, &QTimer::timeout, this, [&] {
    if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
      passphrasePromptShown = true;
      dialog->reject();
    }
  });
  dialogTimer.start(20);
  QTcpServer server;
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  QList<QByteArray> requests;
  crypto::PublicKey clientPublicKey{};
  bool identityPublished = false;
  bool rejectRefresh = false;
  bool holdNextHistory = false;
  QPointer<QTcpSocket> heldHistory;
  QByteArray groupHistory = R"({"messages":[]})";
  int groupHistoryRequests = 0;
  int peerKeyRequests = 0;
  bool showInvitedConversation = false;
  const auto respond = [](QTcpSocket* socket, const QByteArray& body,
                          const QByteArray& status) {
    socket->write("HTTP/1.1 " + status +
                  "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                  QByteArray::number(body.size()) + "\r\n\r\n" + body);
    socket->disconnectFromHost();
  };
  connect(&server, &QTcpServer::newConnection, &server, [&]() {
    while (server.hasPendingConnections()) {
      auto* socket = server.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer]() {
        buffer->append(socket->readAll());
        const auto split = buffer->indexOf("\r\n\r\n");
        if (split < 0) return;
        qsizetype length = 0;
        for (const auto& line : buffer->left(split).split('\n')) {
          if (line.toLower().startsWith("content-length:"))
            length = line.mid(15).trimmed().toLongLong();
        }
        if (buffer->size() < split + 4 + length) return;
        const auto request = *buffer;
        buffer->clear();
        requests.append(request);
        QByteArray body;
        if (request.startsWith("POST /auth/login ")) {
          body = QJsonDocument(QJsonObject{
              {"user_id", localId}, {"access_token", "jwt-first"},
              {"refresh_token", "refresh-first"}}).toJson();
        } else if (request.startsWith("POST /auth/refresh ")) {
          if (rejectRefresh) {
            respond(socket, "{}", "401 Unauthorized");
            return;
          }
          body =
              R"({"access_token":"jwt-rotated","refresh_token":"refresh-rotated"})";
        } else if (request.startsWith("PUT /messaging/identity-key ")) {
          const auto publication = QJsonDocument::fromJson(request.mid(split + 4)).object();
          const auto key = QByteArray::fromBase64(publication.value("public_key").toString().toLatin1());
          if (key.size() == qsizetype(clientPublicKey.size())) {
            std::copy(key.cbegin(), key.cend(), clientPublicKey.begin());
            identityPublished = true;
          }
          body = R"({"status":"ok"})";
        } else if (request.contains("/peer-key HTTP/1.1")) {
          ++peerKeyRequests;
          const auto key = QByteArray(reinterpret_cast<const char*>(offeredKey.data()),
                                      offeredKey.size()).toBase64();
          body = QJsonDocument(QJsonObject{{"user_id", offeredPeer},
                                         {"public_key", QString::fromLatin1(key)}}).toJson();
        } else if (request.startsWith("GET /messaging/conversations ")) {
          body = showInvitedConversation
              ? R"([{"id":"a","name":"Team"},{"id":"b","name":"Team"},{"id":"invited","name":"Invitation"}])"
              : R"([{"id":"a","name":"Team"},{"id":"b","name":"Team"}])";
        } else if (request.startsWith("GET /messaging/conversations/c/messages ")) {
          ++groupHistoryRequests;
          if (holdNextHistory) {
            holdNextHistory = false;
            heldHistory = socket;
            return;
          }
          body = groupHistory;
        } else if (request.startsWith("GET /messaging/conversations/") &&
                   request.contains("/messages HTTP/1.1")) {
          body = R"({"messages":[]})";
        } else {
          body = R"({"id":"c","name":"New group"})";
        }
        respond(socket, body, "200 OK");
      });
    }
  });
  network::HttpClient client(
      QUrl("http://127.0.0.1:" + QString::number(server.serverPort())));
  ui::MainWindow window(client);
  auto* username = window.findChild<QLineEdit*>("usernameInput");
  auto* password = window.findChild<QLineEdit*>("passwordInput");
  auto* list = window.findChild<QListWidget*>("conversationList");
  auto* input = window.findChild<QLineEdit*>("messageInput");
  auto* send = window.findChild<QPushButton*>("sendButton");
  auto* name = window.findChild<QLineEdit*>("conversationNameInput");
  auto* listTimer = window.findChild<QTimer*>("conversationListTimer");
  QVERIFY(listTimer);
  QVERIFY(!listTimer->isActive());
  QVERIFY(username && password && list && input && send && name);
  QVERIFY(!send->isEnabled());
  username->setText("test-user");
  password->setText("test-password");
  // Both fields submit; repeated Enter while pending must not duplicate login.
  QTest::keyClick(password, Qt::Key_Return);
  QTest::keyClick(username, Qt::Key_Return);
  QTRY_COMPARE(list->count(), 2);

  QVERIFY(requests[1].contains("Authorization: Bearer jwt-first"));
  QCOMPARE(list->currentRow(), 0);
  QTRY_VERIFY(send->isEnabled());
  input->setText("message A");
  send->click();
  input->setText("draft A");
  list->setCurrentRow(1);
  QVERIFY(input->text().isEmpty());
  auto* view = window.findChild<QTextEdit*>();
  QVERIFY(view->toPlainText().isEmpty());
  QTRY_VERIFY(send->isEnabled());
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

  QTRY_VERIFY(std::any_of(requests.cbegin(), requests.cend(),
                          [](const QByteArray& request) {
                            return request.startsWith("POST /auth/refresh ");
                          }));
  QTRY_VERIFY(window.findChild<QTimer*>("refreshTimer")->isActive());
  QVERIFY(std::any_of(requests.cbegin(), requests.cend(),
                      [](const QByteArray& request) {
                        return request.startsWith("POST /auth/refresh ") &&
                               request.contains("refresh-first");
                      }));
  name->setText("New group");
  QTest::keyClick(name, Qt::Key_Return);
  QTRY_COMPARE(list->count(), 3);
  QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), QString("c"));
  QTRY_VERIFY(std::any_of(
      requests.cbegin(), requests.cend(), [](const QByteArray& request) {
        return request.startsWith("POST /messaging/conversations ") &&
               request.contains("Authorization: Bearer jwt-rotated");
      }));
  // The second refresh must send the rotated refresh token.
  QVERIFY(QMetaObject::invokeMethod(&window, "refreshAccessToken"));
  QTRY_VERIFY(std::any_of(requests.cbegin(), requests.cend(),
                          [](const QByteArray& request) {
                            return request.startsWith("POST /auth/refresh ") &&
                                   request.contains("refresh-rotated");
                          }));

  auto* historyTimer = window.findChild<QTimer*>("historyTimer");
  QVERIFY(historyTimer);
  QCOMPARE(historyTimer->interval(), 3000);
  QVERIFY(identityPublished);
  auto* securityButton = window.findChild<QPushButton*>("securityButton");
  auto* securityDialog = window.findChild<QDialog*>("securityDialog");
  QVERIFY(securityButton && securityDialog);
  QVERIFY(!securityDialog->isVisible());
  securityButton->click();
  QVERIFY(securityDialog->isVisible());
  auto* ownFingerprint = securityDialog->findChild<QLabel*>("localFingerprintLabel");
  QVERIFY(ownFingerprint);
  QVERIFY(ownFingerprint->text().contains(
      crypto::PeerTrustStore::fingerprint(clientPublicKey)));
  securityDialog->reject();
  QVERIFY(!securityDialog->isVisible());
  QTRY_VERIFY(historyTimer->isActive());
  QTRY_VERIFY(groupHistoryRequests >= 1);

  // Simulate the peer encrypting a message for the selected conversation.
  const auto keys = localId < peerId
      ? crypto::KeyManager::deriveServerSessionKeys(
            peer.publicKey, peer.privateKey, clientPublicKey)
      : crypto::KeyManager::deriveClientSessionKeys(
            peer.publicKey, peer.privateKey, clientPublicKey);
  crypto::SessionKey conversationTx{};
  QCOMPARE(crypto_generichash(conversationTx.data(), conversationTx.size(),
                            reinterpret_cast<const unsigned char*>("c"), 1,
                            keys.tx.data(), keys.tx.size()), 0);
  const QString plaintext = "Automatically received from peer";
  const auto encrypted =
      crypto::MessageCrypto::encrypt(plaintext.toStdString(), conversationTx);
  const auto encode = [](const auto& bytes) {
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(bytes.data()), bytes.size()).toBase64());
  };
  const QJsonObject message{{"message_id", "automatic-1"},
                            {"user_id", peerId},
                            {"encryption_version", 2},
                            {"nonce", encode(encrypted.nonce)},
                            {"ciphertext", encode(encrypted.ciphertext)}};
  groupHistory = QJsonDocument(QJsonObject{{"messages", QJsonArray{message}}}).toJson();

  holdNextHistory = true;
  QTRY_VERIFY_WITH_TIMEOUT(heldHistory, 4500);
  const int heldRequestCount = groupHistoryRequests;
  QVERIFY(!view->toPlainText().contains(plaintext));
  QTest::qWait(historyTimer->interval() + 200);
  QCOMPARE(groupHistoryRequests, heldRequestCount);
  respond(heldHistory, groupHistory, "200 OK");
  heldHistory.clear();

  QTRY_COMPARE(view->toPlainText().count(plaintext), 1);
  QTRY_VERIFY_WITH_TIMEOUT(groupHistoryRequests > heldRequestCount, 4500);
  // Wait for a following request so the previous duplicate response was applied.
  holdNextHistory = true;
  QTRY_VERIFY_WITH_TIMEOUT(heldHistory, 4500);
  QCOMPARE(view->toPlainText().count(plaintext), 1);
  QVERIFY(!passphrasePromptShown);

  // Refresh must check the peer while an old history reply is still in flight.
  auto* reload = window.findChild<QPushButton*>("reloadConversationsButton");
  auto* trustLabel = securityDialog->findChild<QLabel*>("peerTrustLabel");
  auto* securityWarning = window.findChild<QLabel*>("securityWarningLabel");
  QVERIFY(reload && trustLabel && securityWarning);
  QVERIFY(trustLabel->text().contains(crypto::PeerTrustStore::fingerprint(peer.publicKey)));
  offeredKey = replacement.publicKey;
  reload->click();
  QVERIFY(!send->isEnabled());
  QVERIFY(!historyTimer->isActive());
  QTRY_VERIFY(trustLabel->text().contains("peer key changed"));
  QVERIFY(trustLabel->text().contains(crypto::PeerTrustStore::fingerprint(peer.publicKey)));
  QVERIFY(trustLabel->text().contains(crypto::PeerTrustStore::fingerprint(offeredKey)));
  QVERIFY(securityWarning->text().contains("E2EE blocked"));
  QVERIFY(!securityWarning->text().contains(peerId));
  QVERIFY(!securityWarning->text().contains(
      crypto::PeerTrustStore::fingerprint(offeredKey)));
  auto staleMessage = message;
  staleMessage.insert("message_id", "stale-history");
  const auto staleHistory = QJsonDocument(
      QJsonObject{{"messages", QJsonArray{staleMessage}}}).toJson();
  respond(heldHistory, staleHistory, "200 OK");
  QTRY_VERIFY(heldHistory.isNull());
  const int blockedCount = groupHistoryRequests;
  QTest::qWait(historyTimer->interval() + 200);
  QCOMPARE(groupHistoryRequests, blockedCount);
  QCOMPARE(view->toPlainText().count(plaintext), 1);
  QVERIFY(trustLabel->text().contains("peer key changed"));

  // The original key under a different UUID must also be rejected.
  offeredKey = peer.publicKey;
  offeredPeer = otherPeerId;
  QTRY_VERIFY(reload->isEnabled());
  reload->click();
  QTRY_VERIFY(trustLabel->text().contains("conversation peer changed"));
  QVERIFY(trustLabel->text().contains(peerId));
  QVERIFY(trustLabel->text().contains(otherPeerId));
  QVERIFY(!historyTimer->isActive());

  offeredPeer = peerId;
  QTRY_VERIFY(reload->isEnabled());
  holdNextHistory = true;
  reload->click();
  QTRY_VERIFY(send->isEnabled());
  QTRY_VERIFY(heldHistory);
  QVERIFY(historyTimer->isActive());
  QVERIFY(!trustLabel->text().contains("blocked"));
  QVERIFY(!passphrasePromptShown);

  QVERIFY(listTimer->isActive());
  const auto selectedConversation = list->currentItem();
  const auto keyRequestsBeforeRefresh = peerKeyRequests;
  input->setText("draft during background refresh");
  showInvitedConversation = true;
  QTRY_COMPARE_WITH_TIMEOUT(list->count(), 4, 6500);
  QCOMPARE(list->currentItem(), selectedConversation);
  QCOMPARE(input->text(), QString("draft during background refresh"));
  QCOMPARE(peerKeyRequests, keyRequestsBeforeRefresh);
  QVERIFY(historyTimer->isActive());

  // A reply from the previous session must not repopulate the cleared chat.
  rejectRefresh = true;
  QVERIFY(QMetaObject::invokeMethod(&window, "refreshAccessToken"));
  QTRY_VERIFY(!historyTimer->isActive());
  QVERIFY(!listTimer->isActive());
  QCOMPARE(list->count(), 0);
  respond(heldHistory, groupHistory, "200 OK");
  const int requestsAfterReset = groupHistoryRequests;
  QTest::qWait(historyTimer->interval() + 200);
  QCOMPARE(groupHistoryRequests, requestsAfterReset);
  QVERIFY(view->toPlainText().isEmpty());
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
    auto* socket = server.nextPendingConnection();
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
