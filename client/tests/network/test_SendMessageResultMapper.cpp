#include <QtTest/QtTest>

#include "network/SendMessageResultMapper.hpp"
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

class TestSendMessageResultMapper : public QObject {
    Q_OBJECT

private slots:
    void mapsHttp200ToSuccess();
    void mapsHttp401ToUnauthorized();
    void mapsConnectionRefusedToNetworkError();
    void mapsHttp500ToServerError();
};

void TestSendMessageResultMapper::mapsHttp200ToSuccess()
{
    QTcpServer server;

QVERIFY(server.listen(QHostAddress::LocalHost, 0));

quint16 port = server.serverPort();

QUrl url;
url.setScheme("http");
url.setHost("127.0.0.1");
url.setPort(port);

QNetworkAccessManager manager;

QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

QNetworkReply* reply = manager.get(QNetworkRequest(url));

QVERIFY(reply != nullptr);

QVERIFY(connectionSpy.wait(3000));

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

QSignalSpy finishedSpy(reply, &QNetworkReply::finished);

QCOMPARE(
    socket->write(response),
    static_cast<qint64>(response.size())
);

socket->flush();

QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000);

auto result = network::SendMessageResultMapper::fromReply(reply);

QCOMPARE(
    result.status,
    network::dto::SendMessageStatus::Success
);

QCOMPARE(result.httpStatusCode, 200);

QVERIFY(result.errorMessage.isEmpty());

reply->deleteLater();
}

void TestSendMessageResultMapper::mapsHttp401ToUnauthorized()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    quint16 port = server.serverPort();

    QUrl url;
    url.setScheme("http");
    url.setHost("127.0.0.1");
    url.setPort(port);

    QNetworkAccessManager manager;

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    QNetworkReply* reply = manager.get(QNetworkRequest(url));

    QVERIFY(reply != nullptr);

    QVERIFY(connectionSpy.wait(3000));

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

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000);

    auto result = network::SendMessageResultMapper::fromReply(reply);

    QCOMPARE(
        result.status,
        network::dto::SendMessageStatus::Unauthorized
    );

    QCOMPARE(result.httpStatusCode, 401);

    QVERIFY(!result.errorMessage.isEmpty());

    reply->deleteLater();
}

void TestSendMessageResultMapper::mapsConnectionRefusedToNetworkError()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    quint16 port = server.serverPort();

    server.close();

    QVERIFY(!server.isListening());

    QUrl url;
    url.setScheme("http");
    url.setHost("127.0.0.1");
    url.setPort(port);

    QNetworkAccessManager manager;

    QNetworkReply* reply = manager.get(QNetworkRequest(url));

    QVERIFY(reply != nullptr);

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000);

    QCOMPARE(
    reply->error(),
    QNetworkReply::ConnectionRefusedError
);

    auto result = network::SendMessageResultMapper::fromReply(reply);

    QCOMPARE(
        result.status,
        network::dto::SendMessageStatus::NetworkError
    );

    QCOMPARE(result.httpStatusCode, 0);

    QVERIFY(!result.errorMessage.isEmpty());

    reply->deleteLater();
}

void TestSendMessageResultMapper::mapsHttp500ToServerError()
{
    QTcpServer server;

    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    quint16 port = server.serverPort();

    QUrl url;
    url.setScheme("http");
    url.setHost("127.0.0.1");
    url.setPort(port);

    QNetworkAccessManager manager;

    QSignalSpy connectionSpy(&server, &QTcpServer::newConnection);

    QNetworkReply* reply = manager.get(QNetworkRequest(url));

    QVERIFY(reply != nullptr);

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

    QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000);

    auto result = network::SendMessageResultMapper::fromReply(reply);

    QCOMPARE(
        result.status,
        network::dto::SendMessageStatus::ServerError
    );

    QCOMPARE(result.httpStatusCode, 500);

    QVERIFY(!result.errorMessage.isEmpty());

    reply->deleteLater();
}

QTEST_MAIN(TestSendMessageResultMapper)

#include "test_SendMessageResultMapper.moc"