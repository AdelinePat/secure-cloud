#include <QApplication>
#include <QMainWindow>
#include <sodium.h>

#include <iostream>
#include <iomanip>

#include "crypto/KeyManager.hpp"

int main(int argc, char *argv[])
{
     if (sodium_init() < 0)
    {
        return 1;
    }

    crypto::KeyPair aliceKeys =
        crypto::KeyManager::generateKeyPair();

    std::cout << "Alice public key: ";

    for (unsigned char byte : aliceKeys.publicKey)
    {
        std::cout
            << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(byte);
    }

    std::cout << std::dec << '\n';

    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("SecureChat");
    window.resize(800, 600);
    window.show();

    return app.exec();
}