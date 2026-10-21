#include <sodium.h>

#include <QApplication>

#include "ui/MainWindow.hpp"

int main(int argc, char* argv[])
{
    // Initialisation of libsodium
    if (sodium_init() < 0) {
        return 1;
    }

    // Initialisation of the Qt application
    QApplication app(argc, argv);

    // Creation of our SecureChat window
    ui::MainWindow window;
    window.show();

    return app.exec();
}