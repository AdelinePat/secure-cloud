#include "network/FileMessage.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>

namespace network {

namespace {

constexpr qint64 kMaxPlaintextSize = 2LL * 1024 * 1024 * 1024;

template <std::size_t N>
QString toBase64(const std::array<unsigned char, N>& bytes) {
  return QString::fromLatin1(
      QByteArray(reinterpret_cast<const char*>(bytes.data()), N).toBase64());
}

template <std::size_t N>
bool fromBase64(const QJsonValue& value, std::array<unsigned char, N>& out) {
  if (!value.isString()) return false;
  const auto decoded = QByteArray::fromBase64Encoding(
      value.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
  if (!decoded || decoded.decoded.size() != static_cast<qsizetype>(N)) return false;
  std::copy(decoded.decoded.begin(), decoded.decoded.end(), out.begin());
  return true;
}

}  // namespace

QByteArray FileMessageCodec::serialize(const FileMessage& message) {
  QJsonObject json{{"kind", "file"},
                   {"file_id", message.fileId},
                   {"key", toBase64(message.key)},
                   {"header", toBase64(message.header)},
                   {"name", message.name},
                   {"size", message.size},
                   {"mime", message.mime}};
  if (!message.text.isEmpty()) json["text"] = message.text;
  return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

std::optional<FileMessage> FileMessageCodec::parse(const QByteArray& data) {
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(data, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    return std::nullopt;
  }
  const auto json = document.object();

  FileMessage message;
  const QUuid fileId(json.value("file_id").toString());
  const auto size = json.value("size");
  if (json.value("kind").toString() != "file" || fileId.isNull() ||
      !fromBase64(json.value("key"), message.key) ||
      !fromBase64(json.value("header"), message.header) ||
      !json.value("name").isString() || !size.isDouble()) {
    return std::nullopt;
  }

  // JSON numbers are doubles: accept only whole, in-range sizes.
  const double sizeValue = size.toDouble();
  if (sizeValue < 0 || sizeValue > static_cast<double>(kMaxPlaintextSize) ||
      sizeValue != static_cast<double>(static_cast<qint64>(sizeValue))) {
    return std::nullopt;
  }
  for (const char* optional : {"mime", "text"}) {
    if (json.contains(optional) && !json.value(optional).isString()) {
      return std::nullopt;
    }
  }

  message.fileId = fileId.toString(QUuid::WithoutBraces);
  message.name = json.value("name").toString();
  message.size = static_cast<qint64>(sizeValue);
  message.mime = json.value("mime").toString();
  message.text = json.value("text").toString();
  return message;
}

QString safeFileName(const QString& name) {
  // Keep only the last path component, whatever the separator.
  QString result = name.section(QRegularExpression("[/\\\\]"), -1);
  result.removeIf([](QChar c) { return c.category() == QChar::Other_Control; });
  result = result.trimmed();
  // Leading dots would hide the file or mean "." / "..".
  while (result.startsWith('.')) result.remove(0, 1);
  return result.isEmpty() ? QStringLiteral("download") : result;
}

}  // namespace network
