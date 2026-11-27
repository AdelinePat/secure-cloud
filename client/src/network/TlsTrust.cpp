#include "network/TlsTrust.hpp"

#include <QDir>
#include <QFileInfo>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>

namespace network {

QString findDevCaCertificate(const QStringList& startDirectories,
                             int maxLevels) {
  for (const QString& start : startDirectories) {
    QDir dir(start);
    for (int level = 0; level <= maxLevels; ++level) {
      const QFileInfo candidate(dir.filePath(kDevCaRelativePath));
      if (candidate.isFile()) {
        return candidate.absoluteFilePath();
      }
      if (!dir.cdUp()) {
        break;
      }
    }
  }
  return {};
}

bool trustOnlyCaCertificates(const QString& pemPath, QString* error) {
  if (!QSslSocket::supportsSsl()) {
    if (error) {
      *error = "This Qt build has no TLS backend (install Qt's OpenSSL "
               "plugin).";
    }
    return false;
  }

  const auto certificates =
      QSslCertificate::fromPath(pemPath, QSsl::Pem, QSslCertificate::PatternSyntax::FixedString);
  if (certificates.isEmpty()) {
    if (error) {
      *error = "No certificate could be read from " + pemPath;
    }
    return false;
  }

  QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
  configuration.setCaCertificates(certificates);
  configuration.setProtocol(QSsl::TlsV1_2OrLater);
  configuration.setPeerVerifyMode(QSslSocket::VerifyPeer);
  QSslConfiguration::setDefaultConfiguration(configuration);
  return true;
}

}  // namespace network
