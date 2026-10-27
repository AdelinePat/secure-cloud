#pragma once

#include <QHash>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>

#include "crypto/CryptoTypes.hpp"
#include "network/HttpClient.hpp"
#include "network/MessageService.hpp"
#include "ui/ChatMessage.hpp"

namespace ui {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(network::HttpClient& httpClient,
                      QWidget* parent = nullptr);

 private slots:
  void onSendClicked();
  void onLoginClicked();
  void refreshAccessToken();

 private:
  QTextEdit* messageView_;
  QLineEdit* messageInput_;
  QPushButton* sendButton_;
  QHash<QString, ChatMessage> messages_;
  QStringList messageOrder_;
  void refreshMessages();
  void updateMessageStatus(const QString& messageId, MessageStatus status);
  network::MessageService messageService_;
  crypto::SessionKey testSessionKey_{};
  network::HttpClient& httpClient_;
  QLineEdit* usernameInput_;
  QLineEdit* passwordInput_;
  QPushButton* loginButton_;

  QString accessToken_;
  QString refreshToken_;
  QTimer* refreshTimer_;
  QStackedWidget* stackedWidget_;
  QWidget* loginPage_;
  QWidget* chatPage_;
};

}  // namespace ui