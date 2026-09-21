#include <sodium.h>

#include <QApplication>
#include <QMainWindow>
#include <iomanip>
#include <iostream>
#include <vector>

#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"

int main(int argc, char* argv[]) {
  if (sodium_init() < 0) {
    return 1;
  }

  crypto::KeyPair aliceKeys = crypto::KeyManager::generateKeyPair();

  std::cout << "Alice public key: ";

  for (unsigned char byte : aliceKeys.publicKey) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(byte);
  }

  crypto::KeyPair bobKeys = crypto::KeyManager::generateKeyPair();

  std::cout << std::dec << '\n';

  std::cout << "Bob public key: ";

  for (unsigned char byte : bobKeys.publicKey) {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(byte);
  }

  std::cout << std::dec << '\n';

  crypto::SessionKeys aliceSessionKeys =
      crypto::KeyManager::deriveClientSessionKeys(
          aliceKeys.publicKey, aliceKeys.privateKey, bobKeys.publicKey);

  crypto::SessionKeys bobSessionKeys =
      crypto::KeyManager::deriveServerSessionKeys(
          bobKeys.publicKey, bobKeys.privateKey, aliceKeys.publicKey);

  aliceSessionKeys.rx == bobSessionKeys.tx
      ? std::cout << "Alice's receiving key matches Bob's transmitting key\n"
      : std::cout
            << "Alice's receiving key does not match Bob's transmitting key\n";

  bobSessionKeys.rx == aliceSessionKeys.tx
      ? std::cout << "Bob's receiving key matches Alice's transmitting key\n"
      : std::cout
            << "Bob's receiving key does not match Alice's transmitting key\n";

  using Nonce = std::array<unsigned char, crypto_secretbox_NONCEBYTES>;
  using Ciphertext = std::vector<unsigned char>;

  struct encrypted_message {
    Nonce n;
    Ciphertext c;
  };

  std::string message = "Hello Bob!";

  crypto::EncryptedMessage encryptedMessage =
      crypto::MessageCrypto::encrypt(message, aliceSessionKeys.tx);

  QApplication app(argc, argv);

  QMainWindow window;
  window.setWindowTitle("SecureChat");
  window.resize(800, 600);
  window.show();

  return app.exec();
}