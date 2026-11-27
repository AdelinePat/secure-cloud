#pragma once

#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTextBrowser>
#include <QTimer>
#include <optional>

#include "crypto/CryptoTypes.hpp"
#include "network/ConversationService.hpp"
#include "network/FileService.hpp"
#include "network/HttpClient.hpp"
#include "network/MessageService.hpp"
#include "ui/ChatMessage.hpp"

class QDialog;

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
  void onAttachClicked();
  void onMessageLinkClicked(const QUrl& url);
  void refreshAccessToken();

 private:
  QTextBrowser* messageView_;
  QLineEdit* messageInput_;
  QPushButton* sendButton_;
  QPushButton* attachButton_;
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
  bool listingInBackground_ = false;
  QTimer* conversationListTimer_;
  void loadConversations(bool background = false);
  void addConversation(const QString& id, const QString& name, bool select);
  void resetConversations();
  void requestActiveHistory();
  void applyHistory(const QString& conversationId, const QByteArray& response,
                    int httpStatus);
  void refreshMessages();
  QTimer* historyTimer_;
  quint64 selectionGeneration_ = 0;
  bool historyPending_ = false;
  bool peerKeyPending_ = false;
  void updateMessageStatus(const QString& messageId, MessageStatus status);
  network::MessageService messageService_;
  network::ConversationService conversationService_;
  network::FileService fileService_;
  QHash<QString, QString> transferLabels_;  // transfer id -> "Uploading x"
  void setComposerEnabled(bool enabled);
  // Encrypts `plaintext` with the conversation key, shows `message` as
  // sending and posts it.
  void sendEncrypted(const QString& conversationId, ChatMessage message,
                     QByteArray plaintext, int encryptionVersion);
  void connectFileTransfers();
  QLineEdit* conversationNameInput_;
  QLineEdit* conversationParticipantsInput_;
  QPushButton* createConversationButton_;
  QString currentConversationId_;
  std::optional<crypto::KeyPair> identityKeyPair_;
  struct ConversationKeys {
    QString peerId;
    crypto::SessionKeys keys;
  };
  QHash<QString, ConversationKeys> conversationKeys_;
  QHash<QString, QHash<QString, crypto::SessionKeys>> groupConversationKeys_;
  void loadPeerKey(const QString& conversationId, bool revalidate = false);
  void loadGroupPeerKeys(const QString& id, quint64 session, quint64 selection);
  void clearConversationKeys(const QString& id);
  void clearCrypto();
  void showPeerTrustDetails(const QString& details);
  QDialog* securityDialog_;
  QLabel* securityWarningLabel_;
  QLabel* localFingerprintLabel_;
  QLabel* peerTrustLabel_;
  QHash<QString, QString> peerTrustDetails_;
  network::HttpClient& httpClient_;
  QLineEdit* usernameInput_;
  QLineEdit* passwordInput_;
  QPushButton* loginButton_;

  QString currentUserId_;
  QString accessToken_;
  QString refreshToken_;
  QTimer* refreshTimer_;
  QStackedWidget* stackedWidget_;
  QWidget* loginPage_;
  QWidget* chatPage_;
};

}  // namespace ui