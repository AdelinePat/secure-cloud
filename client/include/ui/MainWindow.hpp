#pragma once

#include <QHash>
#include <QLineEdit>
#include <QListWidget>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>

#include "crypto/CryptoTypes.hpp"
#include "network/ConversationService.hpp"
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
  void onCreateConversationClicked();
  void refreshAccessToken();

 private:
  QTextEdit* messageView_;
  QLineEdit* messageInput_;
  QPushButton* sendButton_;
  QHash<QString, ChatMessage> messages_;
  QHash<QString, QStringList> conversationMessageOrder_;
  QHash<QString, QString> conversationDrafts_;
  QListWidget* conversationList_;
  QLabel* conversationTitle_;
  QPushButton* reloadConversationsButton_;
  quint64 sessionGeneration_ = 0;
  quint64 creationSession_ = 0;
  quint64 listingSession_ = 0;
  bool listingPending_ = false;
  void loadConversations();
  void addConversation(const QString& id, const QString& name, bool select);
  void resetConversations();
  void refreshMessages();
  void updateMessageStatus(const QString& messageId, MessageStatus status);
  network::MessageService messageService_;
  network::ConversationService conversationService_;
  QLineEdit* conversationNameInput_;
  QPushButton* createConversationButton_;
  QString currentConversationId_;
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