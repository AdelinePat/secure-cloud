#include <sodium.h>

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QUrl>

#include "network/HttpClient.hpp"
#include "network/TlsTrust.hpp"
#include "ui/MainWindow.hpp"

namespace {

// SECURECHAT_GATEWAY_URL overrides the default gateway address.
QUrl gatewayUrl() {
  const QString fromEnv = qEnvironmentVariable("SECURECHAT_GATEWAY_URL");
  return QUrl(fromEnv.isEmpty() ? QStringLiteral("https://localhost:8080")
                                : fromEnv);
}

// SECURECHAT_CA_FILE overrides the CA; otherwise use the one
// `docker compose up` wrote to certs/ca/ca.crt in the repository.
QString caCertificatePath() {
  const QString fromEnv = qEnvironmentVariable("SECURECHAT_CA_FILE");
  if (!fromEnv.isEmpty()) {
    return fromEnv;
  }
  return network::findDevCaCertificate(
      {QCoreApplication::applicationDirPath(), QDir::currentPath()});
}

}  // namespace

int main(int argc, char* argv[]) {
  // Initialisation of libsodium
  if (sodium_init() < 0) {
    return 1;
  }

  // Initialisation of the Qt application
  QApplication app(argc, argv);

  const QUrl baseUrl = gatewayUrl();

  // The gateway's certificate is issued by the development CA, so trust
  // that CA, and only it, for every request.
  if (baseUrl.scheme() == "https") {
    const QString caPath = caCertificatePath();
    QString error;
    if (caPath.isEmpty()) {
      error = QString("Could not find %1.\n\nStart the backend once with "
                      "`docker compose up` to generate it, or set "
                      "SECURECHAT_CA_FILE to its path.")
                  .arg(network::kDevCaRelativePath);
    }
    if (!caPath.isEmpty() && !network::trustOnlyCaCertificates(caPath, &error)) {
      error = "Could not load the CA certificate:\n" + error;
    }
    if (!error.isEmpty()) {
      QMessageBox::critical(nullptr, "SecureChat", error);
      return 1;
    }
  }

  // Create the HTTP client connected to the API Gateway.
  network::HttpClient httpClient(baseUrl);

  // Creation of our SecureChat window
  ui::MainWindow window(httpClient);
  window.show();

  return app.exec();
}
