#include "crypto/PeerTrustStore.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <stdexcept>

namespace crypto {
namespace {

constexpr qint64 MaxStoreBytes = 16 * 1024 * 1024;

QString canonicalUuid(const QString& value) {
  const QUuid uuid(value);
  return uuid.isNull() ? QString() : uuid.toString(QUuid::WithoutBraces);
}

QByteArray sha256(const QByteArray& bytes) {
  return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool isFingerprint(const QString& value) {
  if (value.size() != 64) return false;
  for (const auto c : value) {
    if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'))) return false;
  }
  return true;
}

QByteArray encode(const QJsonObject& state) {
  const auto json = QJsonDocument(state).toJson(QJsonDocument::Compact);
  return sha256(json) + '\n' + json;
}

QJsonObject readStore(const QString& path, const QString& account) {
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) {
    return {{"version", 1}, {"account", account}, {"peers", QJsonObject{}},
            {"conversations", QJsonObject{}}};
  }

  QFile file(path);
  if (info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
    throw std::runtime_error("Cannot read peer trust store");
  }
  const auto bytes = file.read(MaxStoreBytes + 1);
  const auto state = QJsonDocument::fromJson(bytes.mid(65)).object();
  if (file.error() != QFileDevice::NoError || bytes.size() > MaxStoreBytes ||
      bytes != encode(state) || state.size() != 4 ||
      state.value("version").toDouble() != 1 ||
      state.value("account").toString() != account ||
      !state.value("peers").isObject() || !state.value("conversations").isObject()) {
    throw std::runtime_error("Corrupt or unsupported peer trust store");
  }

  const auto peers = state.value("peers").toObject();
  for (auto it = peers.constBegin(); it != peers.constEnd(); ++it) {
    if (it.key().isEmpty() || it.key() != canonicalUuid(it.key()) ||
        it.key() == account || !isFingerprint(it.value().toString())) {
      throw std::runtime_error("Invalid peer pin in trust store");
    }
  }
  const auto conversations = state.value("conversations").toObject();
  for (auto it = conversations.constBegin(); it != conversations.constEnd(); ++it) {
    if (it.key().isEmpty() || !peers.contains(it.value().toString())) {
      throw std::runtime_error("Invalid conversation binding in trust store");
    }
  }
  return state;
}

void writeStore(const QString& path, const QJsonObject& state) {
  const auto bytes = encode(state);
  QSaveFile file(path);
  file.setDirectWriteFallback(false);
  if (bytes.size() > MaxStoreBytes || !file.open(QIODevice::WriteOnly) ||
      file.write(bytes) != bytes.size() || !file.commit()) {
    throw std::runtime_error("Cannot save peer trust store");
  }
}

}  // namespace

QString PeerTrustStore::fingerprint(const PublicKey& key) {
  return QString::fromLatin1(sha256(QByteArray::fromRawData(
      reinterpret_cast<const char*>(key.data()), key.size())));
}

PeerTrustStore::Result PeerTrustStore::checkOrPin(
    const QString& localUser, const QString& conversation,
    const QString& peerUser, const PublicKey& key) {
  const auto account = canonicalUuid(localUser);
  const auto peer = canonicalUuid(peerUser);
  if (account.isEmpty() || peer.isEmpty() || account == peer ||
      conversation.isEmpty()) {
    throw std::invalid_argument("Invalid account, conversation or peer");
  }
  const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (root.isEmpty() || !QDir(root).mkpath("peer-trust")) {
    throw std::runtime_error("Peer trust directory unavailable");
  }
  const auto conversationUuid = canonicalUuid(conversation);
  const auto& conversationId = conversationUuid.isEmpty() ? conversation : conversationUuid;
  const auto path = QDir(root).filePath("peer-trust/" + account + ".trust");
  QLockFile lock(path + ".lock");
  // A slow writer must keep its lock regardless of its age.
  lock.setStaleLockTime(0);
  if (!lock.tryLock(250)) {
    throw std::runtime_error("Cannot lock peer trust store");
  }

  auto state = readStore(path, account);
  auto peers = state.value("peers").toObject();
  auto conversations = state.value("conversations").toObject();
  const auto presented = fingerprint(key);
  const auto bound = conversations.value(conversationId).toString();
  if (!bound.isEmpty() && bound != peer) {
    return {Status::PeerMismatch, bound, peers.value(bound).toString(), presented};
  }
  const auto pinned = peers.value(peer).toString();
  if (!pinned.isEmpty() && pinned != presented) {
    return {Status::KeyMismatch, peer, pinned, presented};
  }
  if (pinned.isEmpty() || bound.isEmpty()) {
    peers.insert(peer, presented);
    conversations.insert(conversationId, peer);
    state.insert("peers", peers);
    state.insert("conversations", conversations);
    writeStore(path, state);
  }
  return {Status::Trusted, peer, presented, presented};
}

}  // namespace crypto
