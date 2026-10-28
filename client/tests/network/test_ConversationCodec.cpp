#include <QtTest/QtTest>

#include <stdexcept>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "network/ConversationCodec.hpp"
#include "network/dto/Conversation.hpp"

class TestConversationCodec : public QObject {
    Q_OBJECT

private slots:
    void serializesCreateRequest();
    void parsesConversationList();
    void parsesConversationDetails();
    void rejectsInvalidJson();
};

void TestConversationCodec::serializesCreateRequest() {
    network::dto::CreateConversationRequest request;
    request.type = "direct";
    request.participantIds = {"user-1", "user-2"};

    const QByteArray json =
        network::ConversationCodec::serializeCreate(request).toUtf8();

    const QJsonObject object = QJsonDocument::fromJson(json).object();

    QCOMPARE(object.value("type").toString(), QString("direct"));
    QVERIFY(object.value("name").isNull());

    const QJsonArray participants = object.value("participant_ids").toArray();
    QCOMPARE(participants.size(), 2);
    QCOMPARE(participants.at(0).toString(), QString("user-1"));
    QCOMPARE(participants.at(1).toString(), QString("user-2"));
}

void TestConversationCodec::parsesConversationList() {
    const QByteArray json = R"({
        "conversations": [
            {
                "conversation_id": "conversation-1",
                "type": "group",
                "name": "Mission Team",
                "participant_ids": ["user-1", "user-2"],
                "last_message_at": "2026-09-02T14:32:01Z",
                "unread_count": 3
            }
        ]
    })";

    const auto conversations = network::ConversationCodec::parseList(json);

    QCOMPARE(conversations.size(), 1);

    const auto& conversation = conversations.first();
    QCOMPARE(conversation.id, QString("conversation-1"));
    QCOMPARE(conversation.type, QString("group"));
    QCOMPARE(conversation.name, QString("Mission Team"));
    QCOMPARE(conversation.participantIds,
             QStringList({"user-1", "user-2"}));
    QCOMPARE(conversation.lastMessageAt,
             QString("2026-09-02T14:32:01Z"));
    QCOMPARE(conversation.unreadCount, 3);
}

void TestConversationCodec::parsesConversationDetails() {
    const QByteArray json = R"({
        "conversation_id": "conversation-1",
        "type": "group",
        "name": "Mission Team",
        "participants": [
            {
                "user_id": "user-1",
                "role": "admin",
                "joined_at": "2026-08-01T09:00:00Z"
            }
        ],
        "created_at": "2026-08-01T09:00:00Z",
        "creator_id": "user-1"
    })";

    const auto details = network::ConversationCodec::parseDetails(json);

    QCOMPARE(details.id, QString("conversation-1"));
    QCOMPARE(details.name, QString("Mission Team"));
    QCOMPARE(details.creatorId, QString("user-1"));
    QCOMPARE(details.participants.size(), 1);
    QCOMPARE(details.participants.first().userId, QString("user-1"));
    QCOMPARE(details.participants.first().role, QString("admin"));
}

void TestConversationCodec::rejectsInvalidJson() {
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        network::ConversationCodec::parseList(QByteArray("not json")));

    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        network::ConversationCodec::parseDetails(QByteArray("{}")));
}

QTEST_MAIN(TestConversationCodec)

#include "test_ConversationCodec.moc"
