#include <sodium.h>

#include <QApplication>
#include <QUrl>

#include "network/HttpClient.hpp"
#include "ui/MainWindow.hpp"

int main(int argc, char* argv[]) {
  // Initialisation of libsodium
  if (sodium_init() < 0) {
    return 1;
  }

  // Initialisation of the Qt application
  QApplication app(argc, argv);

  // Create the HTTP client connected to the API Gateway.
  network::HttpClient httpClient(QUrl("http://localhost:8080"));

  // Creation of our SecureChat window
  ui::MainWindow window(httpClient);
  window.show();

  return app.exec();
}