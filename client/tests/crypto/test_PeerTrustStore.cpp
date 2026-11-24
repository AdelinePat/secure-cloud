#include "crypto/PeerTrustStore.hpp"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtTest/QtTest>
#include <stdexcept>

namespace {

using Store = crypto::PeerTrustStore;
using Status = Store::Status;

QString uniqueId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString trustFile(const QString& account) {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath("peer-trust/" + account + ".trust");
}

}  // namespace

class TestPeerTrustStore : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

  void preservesPinsAndBindings() {
    const auto account = uniqueId();
    const auto peer = uniqueId();
    const auto otherPeer = uniqueId();
    crypto::PublicKey original{};
    auto replacement = original;
    replacement[0] = 1;
    const auto originalHash = Store::fingerprint(original);
    QCOMPARE(originalHash, QString("66687aadf862bd776c8fc18b8e9f8e20089714856ee233b3902a591d0d5f2925"));
    const auto conversation = uniqueId();
    QCOMPARE(Store::checkOrPin(account, conversation, peer, original).status,
             Status::Trusted);
    QVERIFY(QFile::exists(trustFile(account)));

    const auto changed = Store::checkOrPin(account, "b", peer, replacement);
    QCOMPARE(changed.status, Status::KeyMismatch);
    QCOMPARE(changed.expectedFingerprint, originalHash);
    QCOMPARE(changed.presentedFingerprint, Store::fingerprint(replacement));
    QCOMPARE(Store::checkOrPin(account, "b", otherPeer, original).status, Status::Trusted);

    const auto substituted = Store::checkOrPin(account, conversation.toUpper(), otherPeer, original);
    QCOMPARE(substituted.status, Status::PeerMismatch);
    QCOMPARE(substituted.expectedPeer, peer);
    QCOMPARE(Store::checkOrPin(account, conversation, peer, original).status, Status::Trusted);
  }

  void isolatesAccounts() {
    const auto first = uniqueId();
    const auto second = uniqueId();
    const auto peer = uniqueId();
    crypto::PublicKey firstKey{};
    auto secondKey = firstKey;
    secondKey[0] = 1;
    QCOMPARE(Store::checkOrPin(first, "a", peer, firstKey).status, Status::Trusted);
    QCOMPARE(Store::checkOrPin(second, "a", peer, secondKey).status, Status::Trusted);
    QCOMPARE(Store::checkOrPin(first.toUpper(), "a", peer.toUpper(), secondKey).status,
             Status::KeyMismatch);
  }

  void failsClosedOnLockAndCorruption() {
    const auto account = uniqueId();
    const auto peer = uniqueId();
    crypto::PublicKey key{};
    QCOMPARE(Store::checkOrPin(account, "a", peer, key).status, Status::Trusted);
    const auto path = trustFile(account);
    QLockFile lock(path + ".lock");
    QVERIFY(lock.tryLock());
    QVERIFY_EXCEPTION_THROWN(Store::checkOrPin(account, "a", peer, key),
                             std::runtime_error);
    lock.unlock();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    // Change a checksum digit without making the JSON unreadable.
    const auto first = file.read(1);
    QVERIFY(file.seek(0));
    QCOMPARE(file.write(first == "0" ? "1" : "0"), qint64(1));
    QVERIFY(file.seek(0));
    const auto damaged = file.readAll();
    file.close();
    QVERIFY_EXCEPTION_THROWN(Store::checkOrPin(account, "b", peer, key),
                             std::runtime_error);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), damaged);
  }
};

QTEST_GUILESS_MAIN(TestPeerTrustStore)
#include "test_PeerTrustStore.moc"
