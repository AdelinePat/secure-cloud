#include <sodium.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include "network/FileMessage.hpp"

namespace {

network::FileMessage sample() {
  network::FileMessage message;
  message.fileId = "3bae935a-a384-4e10-bfd6-21295d82cac3";
  randombytes_buf(message.key.data(), message.key.size());
  randombytes_buf(message.header.data(), message.header.size());
  message.name = "scan.pdf";
  message.size = 1548559;
  message.mime = "application/pdf";
  message.text = "Here's the scan";
  return message;
}

QJsonObject sampleJson() {
  return QJsonDocument::fromJson(network::FileMessageCodec::serialize(sample()))
      .object();
}

QByteArray toBytes(const QJsonObject& json) {
  return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

}  // namespace

class TestFileMessage : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { QVERIFY(sodium_init() >= 0); }

  void roundTrip() {
    const auto original = sample();
    const auto parsed =
        network::FileMessageCodec::parse(network::FileMessageCodec::serialize(original));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->fileId, original.fileId);
    QVERIFY(parsed->key == original.key);
    QVERIFY(parsed->header == original.header);
    QCOMPARE(parsed->name, original.name);
    QCOMPARE(parsed->size, original.size);
    QCOMPARE(parsed->mime, original.mime);
    QCOMPARE(parsed->text, original.text);
  }

  void matchesTheDocumentedFormat() {
    const auto json = sampleJson();
    QCOMPARE(json.value("kind").toString(), "file");
    QCOMPARE(json.value("file_id").toString(), sample().fileId);
    QCOMPARE(QByteArray::fromBase64(json.value("key").toString().toLatin1()).size(), 32);
    QCOMPARE(QByteArray::fromBase64(json.value("header").toString().toLatin1()).size(), 24);
    QCOMPARE(json.value("size").toInteger(), 1548559);
  }

  void captionAndMimeAreOptional() {
    auto json = sampleJson();
    json.remove("text");
    json.remove("mime");
    const auto parsed = network::FileMessageCodec::parse(toBytes(json));
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->text.isEmpty());
    QVERIFY(parsed->mime.isEmpty());
  }

  void rejectsInvalidMessages_data() {
    QTest::addColumn<QString>("field");
    QTest::addColumn<QJsonValue>("value");  // Undefined = remove the field

    QTest::newRow("plain text, not json") << "" << QJsonValue();
    QTest::newRow("wrong kind") << "kind" << QJsonValue("image");
    QTest::newRow("no kind") << "kind" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("file_id not a uuid") << "file_id" << QJsonValue("../../etc");
    QTest::newRow("key too short") << "key" << QJsonValue("AAAA");
    QTest::newRow("key not base64") << "key" << QJsonValue("!!!!");
    QTest::newRow("no header") << "header" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("name not a string") << "name" << QJsonValue(3);
    QTest::newRow("negative size") << "size" << QJsonValue(-1);
    QTest::newRow("fractional size") << "size" << QJsonValue(1.5);
    QTest::newRow("size over 2 GiB") << "size" << QJsonValue(3e9);
    QTest::newRow("size as string") << "size" << QJsonValue("12");
    QTest::newRow("text not a string") << "text" << QJsonValue(true);
  }

  void rejectsInvalidMessages() {
    QFETCH(QString, field);
    QFETCH(QJsonValue, value);

    QByteArray data = "hello, this is a normal text message";
    if (!field.isEmpty()) {
      auto json = sampleJson();
      if (value.isUndefined()) {
        json.remove(field);
      } else {
        json[field] = value;
      }
      data = toBytes(json);
    }
    QVERIFY(!network::FileMessageCodec::parse(data).has_value());
  }

  void safeFileNameStripsPathsAndControlCharacters_data() {
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");
    QTest::newRow("plain") << "report.pdf" << "report.pdf";
    QTest::newRow("unix path") << "../../.bashrc" << "bashrc";
    QTest::newRow("windows path") << "C:\\Users\\x\\evil.exe" << "evil.exe";
    QTest::newRow("control chars") << QString("a\nb\x01.txt") << "ab.txt";
    QTest::newRow("only dots") << ".." << "download";
    QTest::newRow("empty") << "" << "download";
    QTest::newRow("trailing slash") << "dir/" << "download";
  }

  void safeFileNameStripsPathsAndControlCharacters() {
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QCOMPARE(network::safeFileName(name), expected);
  }
};

QTEST_APPLESS_MAIN(TestFileMessage)
#include "test_FileMessage.moc"
