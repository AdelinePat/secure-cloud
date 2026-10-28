#include <QtTest/QtTest>

#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include "network/MessageService.hpp"
#include "network/dto/Conversation.hpp"
#include "network/dto/History.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "network/dto/SendMessageResult.hpp"

class TestMessageService : public QObject {
    Q_OBJECT

private slots:
    void emitsSuccessWhenServerReturns200();
    void emitsUnauthorizedWhenServerReturns401();
    void emitsNetworkErrorWhenConnectionRefused();
    void emitsServerErrorWhenServerReturns500();
    void handlesConcurrentMessages();
    void emitsConversationsWhenListSucceeds();
    void emitsHistoryWhenHistorySucceeds();
};

void TestMessageService::emitsSuccessWhenServerReturns200()
{
    qRegisterMetaType<network::dto::SendMessageResult>();

    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::MessageService service(baseUrl);

    network::dto::SendMessageRequest request;

    request.messageId = "message-service-123";
    request.encryptionVersion = 1;
    request.encryptedMessage.nonce.fill(1);
    request.encryptedMessage.ciphertext = {10, 20, 30, 40};
    request.clientTimestamp = "2026-09-25T12:00:00Z";

    QSignalSpy messageSpy(
    &service,
    &network::MessageService::messageSent
);

    QVERIFY(messageSpy.isValid());

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    service.sendMessage(
        "conversation-123",
        "fake-jwt-token",
        request
    );

    if (!server.hasPendingConnections()) {
        QVERIFY(connectionSpy.wait(3000));
    }

    QTcpSocket* socket = server.nextPendingConnection();

    QVERIFY(socket != nullptr);

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

    QCOMPARE(
        socket->write(response),
        static_cast<qint64>(response.size())
    );

    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 1, 10000);

    QList<QVariant> arguments = messageSpy.takeFirst();

    QCOMPARE(arguments.size(), 2);

    QString messageId = arguments.at(0).toString();

    QCOMPARE(
    messageId,
    QString::fromStdString(request.messageId)
);

    auto result =
        arguments.at(1).value<network::dto::SendMessageResult>();

    QCOMPARE(
    result.status,
    network::dto::SendMessageStatus::Success
    );

    QCOMPARE(result.httpStatusCode, 200);

    QVERIFY(result.errorMessage.isEmpty());
}

void TestMessageService::emitsUnauthorizedWhenServerReturns401()
{
    qRegisterMetaType<network::dto::SendMessageResult>();

    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::MessageService service(baseUrl);

    network::dto::SendMessageRequest request;

    request.messageId = "message-service-123";
    request.encryptionVersion = 1;
    request.encryptedMessage.nonce.fill(1);
    request.encryptedMessage.ciphertext = {10, 20, 30, 40};
    request.clientTimestamp = "2026-09-25T12:00:00Z";

    QSignalSpy messageSpy(
    &service,
    &network::MessageService::messageSent
);

    QVERIFY(messageSpy.isValid());

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    service.sendMessage(
        "conversation-123",
        "invalid-jwt-token",
        request
    );

    if (!server.hasPendingConnections()) {
        QVERIFY(connectionSpy.wait(3000));
    }

    QTcpSocket* socket = server.nextPendingConnection();

    QVERIFY(socket != nullptr);

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

    QCOMPARE(
        socket->write(response),
        static_cast<qint64>(response.size())
    );

    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 1, 10000);

    QList<QVariant> arguments = messageSpy.takeFirst();

    QCOMPARE(arguments.size(), 2);

    QString messageId = arguments.at(0).toString();

    QCOMPARE(
    messageId,
    QString::fromStdString(request.messageId)
);

auto result =
    arguments.at(1).value<network::dto::SendMessageResult>();

    QCOMPARE(
    result.status,
    network::dto::SendMessageStatus::Unauthorized
    );

    QCOMPARE(result.httpStatusCode, 401);

    QVERIFY(!result.errorMessage.isEmpty());
}

void TestMessageService::emitsNetworkErrorWhenConnectionRefused()
{
    qRegisterMetaType<network::dto::SendMessageResult>();

    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    quint16 port = server.serverPort();

    server.close();

    QVERIFY(!server.isListening());

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(port);

    network::MessageService service(baseUrl);

    network::dto::SendMessageRequest request;

    request.messageId = "message-network-error";
    request.encryptionVersion = 1;
    request.encryptedMessage.nonce.fill(1);
    request.encryptedMessage.ciphertext = {10, 20, 30, 40};
    request.clientTimestamp = "2026-09-25T12:00:00Z";

    QSignalSpy messageSpy(
        &service,
        &network::MessageService::messageSent
    );

    QVERIFY(messageSpy.isValid());

    service.sendMessage(
        "conversation-123",
        "fake-jwt-token",
        request
    );

    QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 1, 10000);

    QList<QVariant> arguments = messageSpy.takeFirst();

    QCOMPARE(arguments.size(), 2);

    QString messageId = arguments.at(0).toString();

    auto result =
        arguments.at(1).value<network::dto::SendMessageResult>();

    QCOMPARE(
        result.status,
        network::dto::SendMessageStatus::NetworkError
    );

    QCOMPARE(result.httpStatusCode, 0);

    QVERIFY(!result.errorMessage.isEmpty());
}

void TestMessageService::emitsServerErrorWhenServerReturns500()
{
    qRegisterMetaType<network::dto::SendMessageResult>();

    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::MessageService service(baseUrl);

    network::dto::SendMessageRequest request;

    request.messageId = "message-service-123";
    request.encryptionVersion = 1;
    request.encryptedMessage.nonce.fill(1);
    request.encryptedMessage.ciphertext = {10, 20, 30, 40};
    request.clientTimestamp = "2026-09-25T12:00:00Z";

    QSignalSpy messageSpy(
    &service,
    &network::MessageService::messageSent
);

    QVERIFY(messageSpy.isValid());

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    service.sendMessage(
        "conversation-123",
        "invalid-jwt-token",
        request
    );

    if (!server.hasPendingConnections()) {
        QVERIFY(connectionSpy.wait(3000));
    }

    QTcpSocket* socket = server.nextPendingConnection();

    QVERIFY(socket != nullptr);

    QByteArray responseBody = R"({"error":"INTERNAL_SERVER_ERROR"})";

    QByteArray response =
        "HTTP/1.1 500 Internal Server Error\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        QByteArray::number(responseBody.size()) +
        "\r\n"
        "Connection: close\r\n"
        "\r\n" +
        responseBody;

    QCOMPARE(
        socket->write(response),
        static_cast<qint64>(response.size())
    );

    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 1, 10000);

    QList<QVariant> arguments = messageSpy.takeFirst();

    QCOMPARE(arguments.size(), 2);

    QString messageId = arguments.at(0).toString();

    QCOMPARE(
    messageId,
    QString::fromStdString(request.messageId)
);

    auto result =
        arguments.at(1).value<network::dto::SendMessageResult>();

    QCOMPARE(
    result.status,
    network::dto::SendMessageStatus::ServerError
    );

    QCOMPARE(result.httpStatusCode, 500);

    QVERIFY(!result.errorMessage.isEmpty());
}

void TestMessageService::handlesConcurrentMessages()
{
    qRegisterMetaType<network::dto::SendMessageResult>();

    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::MessageService service(baseUrl);

    QSignalSpy messageSpy(
        &service,
        &network::MessageService::messageSent
    );

    QVERIFY(messageSpy.isValid());

    // First message
    network::dto::SendMessageRequest requestA;

    requestA.messageId = "message-A";
    requestA.encryptionVersion = 1;
    requestA.encryptedMessage.nonce.fill(1);
    requestA.encryptedMessage.ciphertext = {10, 20, 30};
    requestA.clientTimestamp = "2026-09-25T12:00:00Z";

    // Second message
    network::dto::SendMessageRequest requestB = requestA;

    requestB.messageId = "message-B";

    // Trigger 2 simultaneous messages
    service.sendMessage(
        "conversation-123",
        "fake-jwt-token",
        requestA
    );

    service.sendMessage(
        "conversation-123",
        "fake-jwt-token",
        requestB
    );

   QList<QTcpSocket*> sockets;

QTRY_VERIFY_WITH_TIMEOUT(
    [&]() {
        while (server.hasPendingConnections()) {
            sockets.append(server.nextPendingConnection());
        }

        return sockets.size() == 2;
    }(),
    10000
);

QCOMPARE(sockets.size(), 2);

QTcpSocket* socketA = nullptr;
QTcpSocket* socketB = nullptr;

for (QTcpSocket* socket : sockets) {

    QByteArray receivedData;

    QElapsedTimer readTimer;
    readTimer.start();

    while (readTimer.elapsed() < 5000) {

        receivedData += socket->readAll();

        if (receivedData.contains("message-A") ||
            receivedData.contains("message-B")) {
            break;
        }

        socket->waitForReadyRead(100);
    }

    if (receivedData.contains("message-A")) {
        socketA = socket;
    }
    else if (receivedData.contains("message-B")) {
        socketB = socket;
    }
}

QVERIFY(socketA != nullptr);
QVERIFY(socketB != nullptr);
QVERIFY(socketA != socketB);

QCOMPARE(sockets.size(), 2);

QByteArray bodyB = R"({"success":true})";

QByteArray responseB =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " +
    QByteArray::number(bodyB.size()) +
    "\r\nConnection: close\r\n\r\n" +
    bodyB;

QCOMPARE(
    socketB->write(responseB),
    static_cast<qint64>(responseB.size())
);

socketB->flush();

QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 1, 10000);

QList<QVariant> argumentsB = messageSpy.at(0);

QCOMPARE(argumentsB.size(), 2);

QString messageIdB = argumentsB.at(0).toString();

auto resultB =
    argumentsB.at(1).value<network::dto::SendMessageResult>();

QCOMPARE(messageIdB, QString("message-B"));

QCOMPARE(
    resultB.status,
    network::dto::SendMessageStatus::Success
);

QCOMPARE(resultB.httpStatusCode, 200);

QByteArray bodyA = R"({"error":"INTERNAL_SERVER_ERROR"})";

QByteArray responseA =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: " +
    QByteArray::number(bodyA.size()) +
    "\r\nConnection: close\r\n\r\n" +
    bodyA;

QCOMPARE(
    socketA->write(responseA),
    static_cast<qint64>(responseA.size())
);

socketA->flush();

QTRY_COMPARE_WITH_TIMEOUT(messageSpy.count(), 2, 10000);

QList<QVariant> argumentsA = messageSpy.at(1);

QCOMPARE(argumentsA.size(), 2);

QString messageIdA = argumentsA.at(0).toString();

auto resultA =
    argumentsA.at(1).value<network::dto::SendMessageResult>();

QCOMPARE(messageIdA, QString("message-A"));

QCOMPARE(
    resultA.status,
    network::dto::SendMessageStatus::ServerError
);

QCOMPARE(resultA.httpStatusCode, 500);

}

void TestMessageService::emitsConversationsWhenListSucceeds()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::MessageService service(baseUrl);

    QSignalSpy conversationsSpy(
        &service,
        &network::MessageService::conversationsLoaded
    );

    QVERIFY(conversationsSpy.isValid());

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    service.listConversations("fake-jwt-token");

    if (!server.hasPendingConnections()) {
        QVERIFY(connectionSpy.wait(3000));
    }

    QTcpSocket* socket = server.nextPendingConnection();

    QVERIFY(socket != nullptr);

    QSignalSpy readyReadSpy(socket, &QTcpSocket::readyRead);

    if (socket->bytesAvailable() == 0) {
        QVERIFY(readyReadSpy.wait(3000));
    }

    const QByteArray request = socket->readAll();

    QVERIFY(request.startsWith(
        "GET /secureChat/conversations HTTP/1.1"));
    QVERIFY(request.contains("Authorization: Bearer fake-jwt-token"));

    const QByteArray responseBody = R"({
        "conversations": [{
            "conversation_id": "conversation-1",
            "type": "direct",
            "name": null,
            "participant_ids": ["user-1", "user-2"],
            "last_message_at": null,
            "unread_count": 2
        }]
    })";

    const QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        QByteArray::number(responseBody.size()) +
        "\r\nConnection: close\r\n\r\n" + responseBody;

    QCOMPARE(socket->write(response),
             static_cast<qint64>(response.size()));
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(conversationsSpy.count(), 1, 10000);

    const QList<QVariant> arguments = conversationsSpy.takeFirst();

    const auto result =
        arguments.at(0).value<network::dto::ConversationsResult>();

    QCOMPARE(result.meta.status,
             network::dto::RequestStatus::Success);
    QCOMPARE(result.conversations.size(), 1);
    QCOMPARE(result.conversations.first().id,
             QString("conversation-1"));
    QCOMPARE(result.conversations.first().unreadCount, 2);
}

void TestMessageService::emitsHistoryWhenHistorySucceeds()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QUrl baseUrl;
    baseUrl.setScheme("http");
    baseUrl.setHost("127.0.0.1");
    baseUrl.setPort(server.serverPort());

    network::MessageService service(baseUrl);

    QSignalSpy historySpy(
        &service,
        &network::MessageService::historyLoaded
    );

    QVERIFY(historySpy.isValid());

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    service.loadHistory("fake-jwt-token", "conversation-1", {}, 10);

    if (!server.hasPendingConnections()) {
        QVERIFY(connectionSpy.wait(3000));
    }

    QTcpSocket* socket = server.nextPendingConnection();

    QVERIFY(socket != nullptr);

    QSignalSpy readyReadSpy(socket, &QTcpSocket::readyRead);

    if (socket->bytesAvailable() == 0) {
        QVERIFY(readyReadSpy.wait(3000));
    }

    const QByteArray request = socket->readAll();

    QVERIFY(request.startsWith(
        "GET /secureChat/conversations/conversation-1/history?limit=10 "
        "HTTP/1.1"));

    const QByteArray responseBody = R"({
        "messages": [{
            "message_id": "message-1",
            "userId": "user-1",
            "ciphertext": "AQIDBA==",
            "nonce": "AQEBAQEBAQEBAQEBAQEBAQEBAQEBAQEB",
            "encryption_version": 1,
            "server_timestamp": "2026-09-02T14:32:01Z",
            "attachments": []
        }],
        "next_cursor": null
    })";

    const QByteArray response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " +
        QByteArray::number(responseBody.size()) +
        "\r\nConnection: close\r\n\r\n" + responseBody;

    QCOMPARE(socket->write(response),
             static_cast<qint64>(response.size()));
    socket->flush();

    QTRY_COMPARE_WITH_TIMEOUT(historySpy.count(), 1, 10000);

    const QList<QVariant> arguments = historySpy.takeFirst();

    QCOMPARE(arguments.at(0).toString(), QString("conversation-1"));

    const auto result =
        arguments.at(1).value<network::dto::HistoryResult>();

    QCOMPARE(result.meta.status,
             network::dto::RequestStatus::Success);
    QCOMPARE(result.page.messages.size(), 1);
    QCOMPARE(result.page.messages.first().messageId,
             QString("message-1"));
    QCOMPARE(result.page.messages.first().userId, QString("user-1"));
    QVERIFY(!result.page.hasNext);
}

QTEST_MAIN(TestMessageService)

#include "test_MessageService.moc"