#pragma once

#include <QString>
#include <QStringList>

namespace network {

// Where the development CA is written by `docker compose up`
// (tools/ensure-certs.sh), relative to the repository root.
inline constexpr char kDevCaRelativePath[] = "certs/ca/ca.crt";

// Looks for certs/ca/ca.crt in each start directory and its parents (up to
// `maxLevels` levels up), so the client finds the CA whether it runs from
// client/build/, client/build/Release/ or the repository root. Returns the
// first match, or an empty string.
QString findDevCaCertificate(const QStringList& startDirectories,
                             int maxLevels = 5);

// Makes every QNetworkAccessManager request trust only the CA certificates
// in `pemPath` (not the system store) and require TLS 1.2 or later.
// Returns false and sets `error` if the file has no usable certificate.
bool trustOnlyCaCertificates(const QString& pemPath, QString* error);

}  // namespace network
