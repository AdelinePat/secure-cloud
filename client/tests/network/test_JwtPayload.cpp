#include <QtTest/QtTest>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "network/JwtPayload.hpp"

namespace {

QString encodeSegment(const QJsonObject& object) {
    return QString::fromUtf8(
        QJsonDocument(object)
            .toJson(QJsonDocument::Compact)
            .toBase64(QByteArray::Base64UrlEncoding |
                      QByteArray::OmitTrailingEquals));
}

}  // namespace

class TestJwtPayload : public QObject {
    Q_OBJECT

private slots:
    void extractsSubject();
    void returnsEmptyForMalformedToken();
    void returnsEmptyWhenSubjectMissing();
};

void TestJwtPayload::extractsSubject() {
    QJsonObject payload;
    payload["sub"] = "user-42";

    const QString jwt =
        encodeSegment(QJsonObject{{"alg", "ES256"}}) + "." +
        encodeSegment(payload) + ".signature";

    QCOMPARE(network::JwtPayload::subject(jwt), QString("user-42"));
}

void TestJwtPayload::returnsEmptyForMalformedToken() {
    QVERIFY(network::JwtPayload::subject("not-a-jwt").isEmpty());
    QVERIFY(network::JwtPayload::subject("a.%%%.c").isEmpty());
}

void TestJwtPayload::returnsEmptyWhenSubjectMissing() {
    const QString jwt =
        encodeSegment(QJsonObject{{"alg", "ES256"}}) + "." +
        encodeSegment(QJsonObject{{"iss", "auth"}}) + ".signature";

    QVERIFY(network::JwtPayload::subject(jwt).isEmpty());
}

QTEST_MAIN(TestJwtPayload)

#include "test_JwtPayload.moc"
