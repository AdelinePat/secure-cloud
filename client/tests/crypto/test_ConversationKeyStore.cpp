#include <sodium.h>

#include <QtTest/QtTest>

#include <stdexcept>

#include "crypto/ConversationKeyStore.hpp"

class TestConversationKeyStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void derivesDeterministicKey();
    void changesWithConversation();
    void changesWithPassphrase();
    void rejectsEmptyInput();
};

void TestConversationKeyStore::initTestCase() {
    QVERIFY(sodium_init() >= 0);
}

void TestConversationKeyStore::derivesDeterministicKey() {
    const auto first =
        crypto::ConversationKeyStore::derive("conversation-1", "secret");
    const auto second =
        crypto::ConversationKeyStore::derive("conversation-1", "secret");

    QVERIFY(first == second);
}

void TestConversationKeyStore::changesWithConversation() {
    const auto first =
        crypto::ConversationKeyStore::derive("conversation-1", "secret");
    const auto second =
        crypto::ConversationKeyStore::derive("conversation-2", "secret");

    QVERIFY(first != second);
}

void TestConversationKeyStore::changesWithPassphrase() {
    const auto first =
        crypto::ConversationKeyStore::derive("conversation-1", "secret");
    const auto second =
        crypto::ConversationKeyStore::derive("conversation-1", "other");

    QVERIFY(first != second);
}

void TestConversationKeyStore::rejectsEmptyInput() {
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        crypto::ConversationKeyStore::derive("", "secret"));

    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        crypto::ConversationKeyStore::derive("conversation-1", ""));
}

QTEST_MAIN(TestConversationKeyStore)

#include "test_ConversationKeyStore.moc"
