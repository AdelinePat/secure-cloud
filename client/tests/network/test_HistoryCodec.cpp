#include <sodium.h>

#include <QtTest/QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <stdexcept>

#include "crypto/MessageSerializer.hpp"
#include "network/HistoryCodec.hpp"
#include "network/dto/History.hpp"

class TestHistoryCodec : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void parsesMessagesAndCursor();
    void parsesMessageWithoutCursor();
    void rejectsInvalidPayload();
};

void TestHistoryCodec::initTestCase() {
    QVERIFY(sodium_init() >= 0);
}

void TestHistoryCodec::parsesMessagesAndCursor() {
    crypto::Nonce nonce;
    nonce.fill(7);

    crypto::Ciphertext ciphertext = {1, 2, 3, 4, 5};

    QJsonObject message;
    message["message_id"] = "message-1";
    message["userId"] = "user-1";
    message["nonce"] = QString::fromStdString(
        crypto::MessageSerializer::nonceToBase64(nonce));
    message["ciphertext"] = QString::fromStdString(
        crypto::MessageSerializer::ciphertextToBase64(ciphertext));
    message["encryption_version"] = 1;
    message["server_timestamp"] = "2026-09-02T14:32:01Z";

    QJsonObject attachment;
    attachment["file_id"] = "file-1";
    attachment["size_bytes"] = 1024;
    attachment["checksum_sha256"] = "abc123";

    QJsonArray attachments;
    attachments.append(attachment);
    message["attachments"] = attachments;

    QJsonArray messages;
    messages.append(message);

    QJsonObject root;
    root["messages"] = messages;
    root["next_cursor"] = "cursor-1";

    const QByteArray json =
        QJsonDocument(root).toJson(QJsonDocument::Compact);

    const auto page = network::HistoryCodec::parse(json);

    QCOMPARE(page.messages.size(), 1);
    QVERIFY(page.hasNext);
    QCOMPARE(page.nextCursor, QString("cursor-1"));

    const auto& parsed = page.messages.first();
    QCOMPARE(parsed.messageId, QString("message-1"));
    QCOMPARE(parsed.userId, QString("user-1"));
    QVERIFY(parsed.encryptedMessage.nonce == nonce);
    QVERIFY(parsed.encryptedMessage.ciphertext == ciphertext);
    QCOMPARE(parsed.serverTimestamp, QString("2026-09-02T14:32:01Z"));
    QCOMPARE(parsed.attachments.size(), 1);
    QCOMPARE(parsed.attachments.first().fileId, QString("file-1"));
    QCOMPARE(parsed.attachments.first().sizeBytes, qint64(1024));
    QCOMPARE(parsed.attachments.first().checksumSha256,
             QString("abc123"));
}

void TestHistoryCodec::parsesMessageWithoutCursor() {
    crypto::Nonce nonce;
    nonce.fill(1);

    QJsonObject message;
    message["message_id"] = "message-2";
    message["user_id"] = "user-2";
    message["nonce"] = QString::fromStdString(
        crypto::MessageSerializer::nonceToBase64(nonce));
    message["ciphertext"] = QString::fromStdString(
        crypto::MessageSerializer::ciphertextToBase64({9, 9}));

    QJsonArray messages;
    messages.append(message);

    QJsonObject root;
    root["messages"] = messages;
    root["next_cursor"] = QJsonValue::Null;

    const auto page = network::HistoryCodec::parse(
        QJsonDocument(root).toJson(QJsonDocument::Compact));

    QCOMPARE(page.messages.size(), 1);
    QCOMPARE(page.messages.first().userId, QString("user-2"));
    QVERIFY(!page.hasNext);
    QVERIFY(page.nextCursor.isEmpty());
}

void TestHistoryCodec::rejectsInvalidPayload() {
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        network::HistoryCodec::parse(QByteArray("not json")));

    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        network::HistoryCodec::parse(QByteArray("{}")));
}

QTEST_MAIN(TestHistoryCodec)

#include "test_HistoryCodec.moc"
