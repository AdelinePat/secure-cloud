#include "network/JwtPayload.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace network {

QString JwtPayload::subject(const QString& jwt) {
  const QStringList parts = jwt.split('.');
  if (parts.size() < 2) return {};

  QByteArray payload = parts.at(1).toUtf8();
  while (payload.size() % 4 != 0) payload.append('=');

  const QByteArray decoded =
      QByteArray::fromBase64(payload, QByteArray::Base64UrlEncoding);

  const QJsonDocument document = QJsonDocument::fromJson(decoded);
  if (!document.isObject()) return {};

  return document.object().value("sub").toString();
}

}  // namespace network
