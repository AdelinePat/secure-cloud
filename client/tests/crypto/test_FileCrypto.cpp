#include <sodium.h>

#include <QtTest/QtTest>
#include <stdexcept>
#include <vector>

#include "crypto/FileCrypto.hpp"

using crypto::Bytes;

namespace {

constexpr std::size_t kChunk = crypto::kFilePlainChunkBytes;

Bytes randomBytes(std::size_t size) {
  Bytes bytes(size);
  randombytes_buf(bytes.data(), bytes.size());
  return bytes;
}

// Encrypts `plain` the way the uploader does: 1 MiB pieces, last one final.
std::vector<Bytes> encryptFile(crypto::FileEncryptor& encryptor,
                               const Bytes& plain) {
  std::vector<Bytes> chunks;
  const auto count = crypto::fileChunkCount(std::int64_t(plain.size()));
  for (std::int64_t i = 0; i < count; ++i) {
    const std::size_t offset = std::size_t(i) * kChunk;
    const std::size_t size = std::min(kChunk, plain.size() - offset);
    chunks.push_back(
        encryptor.encryptChunk(plain.data() + offset, size, i == count - 1));
  }
  return chunks;
}

Bytes decryptFile(crypto::FileDecryptor& decryptor,
                  const std::vector<Bytes>& chunks) {
  Bytes plain;
  for (const auto& chunk : chunks) {
    const auto piece = decryptor.decryptChunk(chunk.data(), chunk.size());
    plain.insert(plain.end(), piece.begin(), piece.end());
  }
  return plain;
}

}  // namespace

class TestFileCrypto : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase() { QVERIFY(sodium_init() >= 0); }

  void sizesMatchTheFileApiContract() {
    QCOMPARE(crypto::kFileChunkOverhead, std::size_t(17));
    QCOMPARE(crypto::fileChunkCount(0), 1);
    QCOMPARE(crypto::fileChunkCount(1), 1);
    QCOMPARE(crypto::fileChunkCount(kChunk), 1);
    QCOMPARE(crypto::fileChunkCount(kChunk + 1), 2);
    QCOMPARE(crypto::fileCiphertextSize(0), 17);
    QCOMPARE(crypto::fileCiphertextSize(1548559), 1548559 + 2 * 17);
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument, crypto::fileChunkCount(-1));
  }

  void roundTrip_data() {
    QTest::addColumn<qint64>("size");
    QTest::newRow("empty") << qint64(0);
    QTest::newRow("small") << qint64(5);
    QTest::newRow("exactly one chunk") << qint64(kChunk);
    QTest::newRow("one byte over") << qint64(kChunk + 1);
    QTest::newRow("several chunks") << qint64(2 * kChunk + kChunk / 2);
  }

  void roundTrip() {
    QFETCH(qint64, size);
    const Bytes plain = randomBytes(std::size_t(size));

    crypto::FileEncryptor encryptor;
    const auto chunks = encryptFile(encryptor, plain);

    QCOMPARE(std::int64_t(chunks.size()), crypto::fileChunkCount(size));
    std::int64_t total = 0;
    for (const auto& chunk : chunks) {
      QVERIFY(chunk.size() <= kChunk + 17);
      total += std::int64_t(chunk.size());
    }
    QCOMPARE(total, crypto::fileCiphertextSize(size));

    crypto::FileDecryptor decryptor(encryptor.key(), encryptor.header());
    QVERIFY(decryptFile(decryptor, chunks) == plain);
    QVERIFY(decryptor.finished());
  }

  void eachFileGetsItsOwnKey() {
    crypto::FileEncryptor a;
    crypto::FileEncryptor b;
    QVERIFY(a.key() != b.key());
    QVERIFY(a.header() != b.header());
  }

  void cannotEncryptAfterTheLastChunk() {
    crypto::FileEncryptor encryptor;
    const Bytes piece = randomBytes(10);
    encryptor.encryptChunk(piece.data(), piece.size(), true);
    QVERIFY_THROWS_EXCEPTION(
        std::logic_error, encryptor.encryptChunk(piece.data(), piece.size(), true));
  }

  void rejectsOversizedPlaintextPiece() {
    crypto::FileEncryptor encryptor;
    const Bytes piece(kChunk + 1);
    QVERIFY_THROWS_EXCEPTION(
        std::invalid_argument,
        encryptor.encryptChunk(piece.data(), piece.size(), true));
  }

  void detectsModifiedChunk() {
    crypto::FileEncryptor encryptor;
    auto chunks = encryptFile(encryptor, randomBytes(2 * kChunk));
    chunks[1][100] ^= 1;

    crypto::FileDecryptor decryptor(encryptor.key(), encryptor.header());
    decryptor.decryptChunk(chunks[0].data(), chunks[0].size());
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        decryptor.decryptChunk(chunks[1].data(), chunks[1].size()));
  }

  void detectsReorderedChunks() {
    crypto::FileEncryptor encryptor;
    const auto chunks = encryptFile(encryptor, randomBytes(2 * kChunk + 10));

    crypto::FileDecryptor decryptor(encryptor.key(), encryptor.header());
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        decryptor.decryptChunk(chunks[1].data(), chunks[1].size()));
  }

  void detectsTruncation() {
    crypto::FileEncryptor encryptor;
    const auto chunks = encryptFile(encryptor, randomBytes(2 * kChunk + 10));

    // Every chunk but the last decrypts fine, yet the file is not finished.
    crypto::FileDecryptor decryptor(encryptor.key(), encryptor.header());
    decryptFile(decryptor, {chunks[0], chunks[1]});
    QVERIFY(!decryptor.finished());
  }

  void rejectsDataAfterTheLastChunk() {
    crypto::FileEncryptor encryptor;
    const auto chunks = encryptFile(encryptor, randomBytes(10));

    crypto::FileDecryptor decryptor(encryptor.key(), encryptor.header());
    decryptor.decryptChunk(chunks[0].data(), chunks[0].size());
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        decryptor.decryptChunk(chunks[0].data(), chunks[0].size()));
  }

  void rejectsWrongKeyOrHeader() {
    crypto::FileEncryptor encryptor;
    crypto::FileEncryptor other;
    const auto chunks = encryptFile(encryptor, randomBytes(10));

    crypto::FileDecryptor wrongKey(other.key(), encryptor.header());
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        wrongKey.decryptChunk(chunks[0].data(), chunks[0].size()));

    crypto::FileDecryptor wrongHeader(encryptor.key(), other.header());
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        wrongHeader.decryptChunk(chunks[0].data(), chunks[0].size()));
  }

  void rejectsChunksOfImpossibleSize() {
    crypto::FileEncryptor encryptor;
    crypto::FileDecryptor decryptor(encryptor.key(), encryptor.header());
    const Bytes tooShort(16);
    const Bytes tooLong(kChunk + 18);
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error, decryptor.decryptChunk(tooShort.data(), tooShort.size()));
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error, decryptor.decryptChunk(tooLong.data(), tooLong.size()));
  }
};

QTEST_APPLESS_MAIN(TestFileCrypto)
#include "test_FileCrypto.moc"
