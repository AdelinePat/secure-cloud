#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "network/HttpClient.hpp"
#include "network/TlsTrust.hpp"

namespace {

// Throwaway self-signed certificate for localhost / 127.0.0.1, valid until
// 2126, used only by these tests. It acts as its own CA.
const QByteArray kTestCertificate = R"(-----BEGIN CERTIFICATE-----
MIIBejCCASCgAwIBAgIUPiwSK83jETwnrKbXkaat6z34uaMwCgYIKoZIzj0EAwIw
FDESMBAGA1UEAwwJbG9jYWxob3N0MCAXDTI2MTEyNzEzMjUzOVoYDzIxMjYxMTAz
MTMyNTM5WjAUMRIwEAYDVQQDDAlsb2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjO
PQMBBwNCAAQaez8jtXUcAWwMte4moU5lPYD3zaOxGbZ5esg5ctMwb2Xz5uGTzl3V
eueZMZEc4xChHNi1Blzyf9pfUK7f6xIyo04wTDAdBgNVHQ4EFgQUAeYMopKOjcnd
0tAeLXXOyS67KBkwGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMA8GA1UdEwEB
/wQFMAMBAf8wCgYIKoZIzj0EAwIDSAAwRQIhAObN4AT2CxGdo23v6Aae11jkRstx
2FJ4TIjUyc89XzYsAiAJ8CMOT8nS+5un+g4dMPRWbbvpIUvpnN3xCl8yGSOIsw==
-----END CERTIFICATE-----
)";

const QByteArray kTestKey = R"(-----BEGIN PRIVATE KEY-----
MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg6RW/MuuV6LAzZBWU
NMroA6hrNyDxGQ0ZA8cWWUKVcx+hRANCAAQaez8jtXUcAWwMte4moU5lPYD3zaOx
GbZ5esg5ctMwb2Xz5uGTzl3VeueZMZEc4xChHNi1Blzyf9pfUK7f6xIy
-----END PRIVATE KEY-----
)";

void writeFile(const QString& path, const QByteArray& content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write(content);
}

}  // namespace

class TestTlsTrust : public QObject {
  Q_OBJECT

 private slots:
  void init();

  void findDevCaCertificate_walksUpFromStartDirectory();
  void findDevCaCertificate_returnsEmptyWhenMissing();
  void trustOnlyCaCertificates_rejectsFileWithoutCertificate();
  void trustOnlyCaCertificates_replacesSystemStore();

  void request_succeedsWhenGatewayCertificateIsTrusted();
  void request_failsWhenGatewayCertificateIsNotTrusted();

 private:
  // HTTPS server answering every request with 200 {}.
  void startServer();
  QNetworkReply* login();

  QTemporaryDir dir_;
  std::unique_ptr<QSslServer> server_;
  std::unique_ptr<network::HttpClient> client_;
  QSslConfiguration originalDefault_ = QSslConfiguration::defaultConfiguration();
};

void TestTlsTrust::init() {
  // trustOnlyCaCertificates changes a process-wide default.
  QSslConfiguration::setDefaultConfiguration(originalDefault_);
  server_.reset();
  client_.reset();
}

void TestTlsTrust::findDevCaCertificate_walksUpFromStartDirectory() {
  const QString ca = dir_.filePath("repo/certs/ca/ca.crt");
  writeFile(ca, kTestCertificate);
  QDir().mkpath(dir_.filePath("repo/client/build/Release"));

  const QString found = network::findDevCaCertificate(
      {dir_.filePath("elsewhere"), dir_.filePath("repo/client/build/Release")});
  QCOMPARE(found, QFileInfo(ca).absoluteFilePath());
}

void TestTlsTrust::findDevCaCertificate_returnsEmptyWhenMissing() {
  QDir().mkpath(dir_.filePath("a/b/c"));
  QVERIFY(network::findDevCaCertificate({dir_.filePath("a/b/c")}, 2).isEmpty());
}

void TestTlsTrust::trustOnlyCaCertificates_rejectsFileWithoutCertificate() {
  const QString path = dir_.filePath("not-a-cert.pem");
  writeFile(path, "hello");
  QString error;
  QVERIFY(!network::trustOnlyCaCertificates(path, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!network::trustOnlyCaCertificates(dir_.filePath("missing.pem"), &error));
}

void TestTlsTrust::startServer() {
  QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
  configuration.setLocalCertificate(QSslCertificate(kTestCertificate));
  configuration.setPrivateKey(QSslKey(kTestKey, QSsl::Ec));
  configuration.setPeerVerifyMode(QSslSocket::VerifyNone);

  server_ = std::make_unique<QSslServer>();
  server_->setSslConfiguration(configuration);
  connect(server_.get(), &QTcpServer::pendingConnectionAvailable, this, [this] {
    auto* socket = qobject_cast<QSslSocket*>(server_->nextPendingConnection());
    connect(socket, &QSslSocket::readyRead, socket, [socket] {
      socket->readAll();
      socket->write(
          "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
          "Content-Length: 2\r\nConnection: close\r\n\r\n{}");
      socket->disconnectFromHost();
    });
  });
  QVERIFY(server_->listen(QHostAddress::LocalHost, 0));

  QUrl url;
  url.setScheme("https");
  url.setHost("localhost");
  url.setPort(server_->serverPort());
  client_ = std::make_unique<network::HttpClient>(url);
}

QNetworkReply* TestTlsTrust::login() {
  QNetworkReply* reply = client_->login("alice", "password");
  [&] { QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 10000); }();
  return reply;
}

void TestTlsTrust::request_succeedsWhenGatewayCertificateIsTrusted() {
  if (!QSslSocket::supportsSsl()) QSKIP("No TLS backend");
  const QString ca = dir_.filePath("ca.crt");
  writeFile(ca, kTestCertificate);
  QString error;
  QVERIFY2(network::trustOnlyCaCertificates(ca, &error), qPrintable(error));

  startServer();
  QNetworkReply* reply = login();
  QCOMPARE(reply->error(), QNetworkReply::NoError);
  QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
           200);
  reply->deleteLater();
}

void TestTlsTrust::request_failsWhenGatewayCertificateIsNotTrusted() {
  if (!QSslSocket::supportsSsl()) QSKIP("No TLS backend");
  // Default trust (system store) does not know the development CA.
  startServer();
  QNetworkReply* reply = login();
  QCOMPARE(reply->error(), QNetworkReply::SslHandshakeFailedError);
  reply->deleteLater();
}

void TestTlsTrust::trustOnlyCaCertificates_replacesSystemStore() {
  if (!QSslSocket::supportsSsl()) QSKIP("No TLS backend");
  const QString ca = dir_.filePath("ca.crt");
  writeFile(ca, kTestCertificate);
  QString error;
  QVERIFY2(network::trustOnlyCaCertificates(ca, &error), qPrintable(error));

  const auto trusted = QSslConfiguration::defaultConfiguration().caCertificates();
  QCOMPARE(trusted.size(), 1);
  QCOMPARE(trusted.first(), QSslCertificate(kTestCertificate));
}

QTEST_MAIN(TestTlsTrust)

#include "test_TlsTrust.moc"
