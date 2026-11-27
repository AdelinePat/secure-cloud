#include "ui/MainWindow.hpp"

#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include "crypto/IdentityKeyStore.hpp"
#include "crypto/KeyManager.hpp"
#include "crypto/MessageCrypto.hpp"
#include "crypto/PeerTrustStore.hpp"
#include "network/FileMessage.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "ui/MessageStatusMapper.hpp"

namespace ui {

MainWindow::MainWindow(network::HttpClient& httpClient, QWidget* parent)
    : QMainWindow(parent),
      messageService_(httpClient.baseUrl(), this),
      conversationService_(httpClient.baseUrl(), this),
      fileService_(
          httpClient.baseUrl(), [this] { return accessToken_; }, this),
      httpClient_(httpClient) {
  setWindowTitle("SecureChat");
  resize(900, 600);

  // Create the stacked widget to manage application pages.
  stackedWidget_ = new QStackedWidget(this);
  setCentralWidget(stackedWidget_);

  // Create the login page.
  loginPage_ = new QWidget(stackedWidget_);

  usernameInput_ = new QLineEdit(loginPage_);
  usernameInput_->setObjectName("usernameInput");
  usernameInput_->setPlaceholderText("Username");

  passwordInput_ = new QLineEdit(loginPage_);
  passwordInput_->setObjectName("passwordInput");
  passwordInput_->setPlaceholderText("Password");
  passwordInput_->setEchoMode(QLineEdit::Password);

  loginButton_ = new QPushButton("Login", loginPage_);
  loginButton_->setObjectName("loginButton");

  QVBoxLayout* loginLayout = new QVBoxLayout(loginPage_);
  loginLayout->addStretch();
  loginLayout->addWidget(usernameInput_);
  loginLayout->addWidget(passwordInput_);
  loginLayout->addWidget(loginButton_);
  loginLayout->addStretch();

  // Create the chat page.
  chatPage_ = new QWidget(stackedWidget_);

  conversationNameInput_ = new QLineEdit(chatPage_);
  conversationNameInput_->setObjectName("conversationNameInput");
  conversationNameInput_->setPlaceholderText("Conversation name");
  conversationParticipantsInput_ = new QLineEdit(chatPage_);
  conversationParticipantsInput_->setObjectName(
      "conversationParticipantsInput");
  conversationParticipantsInput_->setPlaceholderText(
      "Participants (e.g. bob_test, charlie_test)");
  createConversationButton_ = new QPushButton("Create conversation", chatPage_);
  createConversationButton_->setObjectName("createConversationButton");

  QHBoxLayout* conversationLayout = new QHBoxLayout();
  conversationLayout->addWidget(conversationNameInput_);
  conversationLayout->addWidget(conversationParticipantsInput_);
  conversationLayout->addWidget(createConversationButton_);

  messageView_ = new QTextBrowser(chatPage_);
  messageView_->setObjectName("messageView");
  // File links are handled by onMessageLinkClicked, never opened directly.
  messageView_->setOpenLinks(false);

  messageInput_ = new QLineEdit(chatPage_);
  messageInput_->setObjectName("messageInput");
  messageInput_->setPlaceholderText("Type a message...");

  sendButton_ = new QPushButton("Send", chatPage_);
  sendButton_->setObjectName("sendButton");

  attachButton_ = new QPushButton("Attach file", chatPage_);
  attachButton_->setObjectName("attachButton");
  attachButton_->setToolTip(
      "Encrypt and send a file. Text in the message box "
      "is sent as its caption.");

  QHBoxLayout* inputLayout = new QHBoxLayout();
  inputLayout->addWidget(messageInput_);
  inputLayout->addWidget(attachButton_);
  inputLayout->addWidget(sendButton_);

  conversationList_ = new QListWidget(chatPage_);
  conversationList_->setObjectName("conversationList");
  conversationList_->setMinimumWidth(180);
  conversationTitle_ = new QLabel("Select a conversation", chatPage_);
  conversationTitle_->setTextFormat(Qt::PlainText);
  reloadConversationsButton_ =
      new QPushButton("Refresh conversations", chatPage_);

  reloadConversationsButton_->setObjectName("reloadConversationsButton");
  auto* securityButton = new QPushButton("Security", chatPage_);
  securityButton->setObjectName("securityButton");
  securityDialog_ = new QDialog(this);
  securityDialog_->setObjectName("securityDialog");
  securityDialog_->setWindowTitle("Security");
  securityDialog_->resize(640, 220);
  connect(securityButton, &QPushButton::clicked, securityDialog_,
          &QDialog::open);

  localFingerprintLabel_ =
      new QLabel("Local fingerprint unavailable.", securityDialog_);
  localFingerprintLabel_->setObjectName("localFingerprintLabel");
  localFingerprintLabel_->setTextFormat(Qt::PlainText);
  localFingerprintLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                                  Qt::TextSelectableByKeyboard);
  localFingerprintLabel_->setWordWrap(true);
  peerTrustLabel_ = new QLabel("Select a conversation to check peer identity.",
                               securityDialog_);
  peerTrustLabel_->setObjectName("peerTrustLabel");
  peerTrustLabel_->setTextFormat(Qt::PlainText);
  peerTrustLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                           Qt::TextSelectableByKeyboard);
  peerTrustLabel_->setWordWrap(true);

  auto* closeSecurity =
      new QDialogButtonBox(QDialogButtonBox::Close, securityDialog_);
  connect(closeSecurity, &QDialogButtonBox::rejected, securityDialog_,
          &QDialog::reject);
  auto* securityLayout = new QVBoxLayout(securityDialog_);
  securityLayout->addWidget(localFingerprintLabel_);
  securityLayout->addWidget(peerTrustLabel_);
  securityLayout->addStretch();
  securityLayout->addWidget(closeSecurity);

  securityWarningLabel_ = new QLabel(chatPage_);
  securityWarningLabel_->setObjectName("securityWarningLabel");
  securityWarningLabel_->setTextFormat(Qt::PlainText);
  securityWarningLabel_->setWordWrap(true);
  securityWarningLabel_->hide();

  auto* sidebar = new QWidget(chatPage_);
  auto* sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->addWidget(new QLabel("Conversations", sidebar));
  sidebarLayout->addWidget(conversationList_);
  sidebarLayout->addWidget(reloadConversationsButton_);
  auto* conversationPanel = new QWidget(chatPage_);
  auto* panelLayout = new QVBoxLayout(conversationPanel);
  auto* conversationHeader = new QHBoxLayout();
  conversationHeader->addWidget(conversationTitle_, 1);
  conversationHeader->addWidget(securityButton);
  panelLayout->addLayout(conversationHeader);
  panelLayout->addWidget(securityWarningLabel_);
  panelLayout->addWidget(messageView_);
  panelLayout->addLayout(inputLayout);
  auto* splitter = new QSplitter(chatPage_);
  splitter->addWidget(sidebar);
  splitter->addWidget(conversationPanel);
  splitter->setStretchFactor(1, 1);
  QVBoxLayout* chatLayout = new QVBoxLayout(chatPage_);
  chatLayout->addLayout(conversationLayout);
  chatLayout->addWidget(splitter);
  setComposerEnabled(false);
  messageInput_->setEnabled(false);

  historyTimer_ = new QTimer(this);
  historyTimer_->setObjectName("historyTimer");
  historyTimer_->setInterval(3000);
  historyTimer_->setTimerType(Qt::PreciseTimer);
  connect(historyTimer_, &QTimer::timeout, this,
          &MainWindow::requestActiveHistory);

  connect(
      conversationList_, &QListWidget::currentItemChanged, this,
      [this](QListWidgetItem* current, QListWidgetItem*) {
        if (!currentConversationId_.isEmpty()) {
          conversationDrafts_[currentConversationId_] = messageInput_->text();
        }

        historyTimer_->stop();
        ++selectionGeneration_;
        peerKeyPending_ = false;
        currentConversationId_ =
            current ? current->data(Qt::UserRole).toString() : QString();

        conversationTitle_->setText(current ? current->text()
                                            : "Select a conversation");

        messageInput_->setText(
            conversationDrafts_.value(currentConversationId_));

        setComposerEnabled(false);
        messageInput_->setEnabled(current != nullptr);

        showPeerTrustDetails(peerTrustDetails_.value(currentConversationId_));

        // Show cached messages for the selected conversation.
        refreshMessages();

        // Load the peer key before requesting history.
        if (!currentConversationId_.isEmpty() && !accessToken_.isEmpty()) {
          loadPeerKey(currentConversationId_);
        }
      });
  connect(reloadConversationsButton_, &QPushButton::clicked, this, [this] {
    loadPeerKey(currentConversationId_, true);
    loadConversations();
  });

  conversationListTimer_ = new QTimer(this);
  conversationListTimer_->setObjectName("conversationListTimer");
  conversationListTimer_->setInterval(5000);
  connect(conversationListTimer_, &QTimer::timeout, this,
          [this] { loadConversations(true); });

  // Register both pages.
  stackedWidget_->addWidget(loginPage_);
  stackedWidget_->addWidget(chatPage_);

  // Show the login page when the application starts.
  stackedWidget_->setCurrentWidget(loginPage_);
  resize(400, 250);

  // Initialize the token refresh timer.
  refreshTimer_ = new QTimer(this);
  refreshTimer_->setObjectName("refreshTimer");
  refreshTimer_->setInterval(14 * 60 * 1000);

  connect(refreshTimer_, &QTimer::timeout, this,
          &MainWindow::refreshAccessToken);

  connect(sendButton_, &QPushButton::clicked, this, &MainWindow::onSendClicked);
  connect(attachButton_, &QPushButton::clicked, this,
          &MainWindow::onAttachClicked);
  connect(messageView_, &QTextBrowser::anchorClicked, this,
          &MainWindow::onMessageLinkClicked);
  connectFileTransfers();

  connect(messageInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onSendClicked);

  connect(&messageService_, &network::MessageService::messageSent, this,
          [this](const QString& messageId,
                 const network::dto::SendMessageResult& result) {
            // Convert the network result into a UI status
            MessageStatus status = MessageStatusMapper::fromResult(result);

            // Update the corresponding message
            updateMessageStatus(messageId, status);
          });

  connect(createConversationButton_, &QPushButton::clicked, this,
          &MainWindow::onCreateConversationClicked);
  connect(conversationNameInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onCreateConversationClicked);

  connect(
      &conversationService_, &network::ConversationService::conversationCreated,
      this, [this](const QString& conversationId, const QString& name) {
        createConversationButton_->setEnabled(true);
        conversationNameInput_->setEnabled(true);
        conversationParticipantsInput_->setEnabled(true);
        if (accessToken_.isEmpty() || creationSession_ != sessionGeneration_) {
          return;
        }
        addConversation(conversationId, name, true);
        loadConversations();
        conversationNameInput_->clear();
        conversationParticipantsInput_->clear();
        statusBar()->showMessage("Conversation created: " + name, 5000);
      });

  connect(
      &conversationService_,
      &network::ConversationService::conversationCreationFailed, this,
      [this](const QString& error) {
        createConversationButton_->setEnabled(true);
        conversationNameInput_->setEnabled(true);
        conversationParticipantsInput_->setEnabled(true);
        if (accessToken_.isEmpty() || creationSession_ != sessionGeneration_) {
          return;
        }
        statusBar()->showMessage("Conversation creation failed: " + error);
      });

  connect(
      &conversationService_, &network::ConversationService::conversationsLoaded,
      this, [this](const QJsonArray& conversations) {
        listingPending_ = false;
        reloadConversationsButton_->setEnabled(true);
        if (accessToken_.isEmpty() || listingSession_ != sessionGeneration_) {
          if (!accessToken_.isEmpty()) loadConversations();
          return;
        }
        for (const auto& value : conversations) {
          const auto conversation = value.toObject();
          addConversation(conversation.value("id").toString(),
                          conversation.value("name").toString(), false);
        }
        if (!conversationList_->currentItem() &&
            conversationList_->count() > 0) {
          conversationList_->setCurrentRow(0);
        }
        if (!listingInBackground_) {
          statusBar()->showMessage(
              conversationList_->count() == 0
                  ? "No conversations yet. Create one to begin."
                  : "Conversations loaded",
              5000);
        }
      });
  connect(
      &conversationService_,
      &network::ConversationService::conversationsLoadFailed, this,
      [this](const QString& error) {
        listingPending_ = false;
        reloadConversationsButton_->setEnabled(true);
        if (accessToken_.isEmpty()) return;
        if (listingSession_ != sessionGeneration_) {
          loadConversations();
          return;
        }
        if (!listingInBackground_) {
          statusBar()->showMessage("Could not load conversations: " + error);
        }
      });
  connect(loginButton_, &QPushButton::clicked, this,
          &MainWindow::onLoginClicked);
  connect(usernameInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onLoginClicked);
  connect(passwordInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onLoginClicked);
}

void MainWindow::onLoginClicked() {
  if (!loginButton_->isEnabled() ||
      stackedWidget_->currentWidget() != loginPage_) {
    return;
  }
  QString username = usernameInput_->text().trimmed();
  QString password = passwordInput_->text();

  if (username.isEmpty() || password.isEmpty()) {
    qWarning() << "Username and password are required";
    return;
  }

  loginButton_->setEnabled(false);

  QNetworkReply* reply = httpClient_.login(username, password);

  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, username, password]() {
        loginButton_->setEnabled(true);

        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError || status != 200) {
          qWarning() << "Login failed:" << status << reply->errorString();
          reply->deleteLater();
          stackedWidget_->setCurrentWidget(loginPage_);
          resize(400, 250);
          return;
        }

        const QByteArray response = reply->readAll();

        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(response, &parseError);

        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject()) {
          qWarning() << "Invalid login response";
          reply->deleteLater();
          return;
        }

        const QJsonObject json = document.object();

        const QString userId = json.value("user_id").toString();
        const QString accessToken = json.value("access_token").toString();
        const QString refreshToken = json.value("refresh_token").toString();

        if (QUuid(userId).isNull() || accessToken.isEmpty() ||
            refreshToken.isEmpty()) {
          qWarning() << "Missing authentication tokens";
          reply->deleteLater();
          return;
        }

        reply->deleteLater();
        resetConversations();
        loginButton_->setEnabled(false);
        currentUserId_ = QUuid(userId).toString(QUuid::WithoutBraces);
        accessToken_ = accessToken;
        refreshToken_ = refreshToken;

        try {
          // Legacy separate identity passphrase flow (disabled).
          /*
          const bool exists = crypto::IdentityKeyStore::exists(userId);
          bool accepted = false;
          const auto passphrase = QInputDialog::getText(this, "SecureChat -
          Identity", exists ? "Enter your identity passphrase:" : "Choose your
          identity passphrase:", QLineEdit::Password, QString(), &accepted); if
          (!accepted || passphrase.isEmpty()) throw std::runtime_error("Identity
          cancelled"); if (exists) { identityKeyPair_ =
          crypto::IdentityKeyStore::load(userId, passphrase); } else { const
          auto confirmation = QInputDialog::getText(this, "Confirm passphrase",
                "Confirm your passphrase:", QLineEdit::Password, QString(),
          &accepted); if (!accepted || confirmation != passphrase) throw
          std::runtime_error("Passphrase mismatch"); auto pair =
          crypto::KeyManager::generateKeyPair();
            crypto::IdentityKeyStore::save(userId, pair, passphrase);
            identityKeyPair_ = pair;
            sodium_memzero(pair.privateKey.data(), pair.privateKey.size());
          }
          */
          // Use the login password to protect the local identity key.
          if (crypto::IdentityKeyStore::exists(userId)) {
            // Legacy passphrase migration disabled; retain for future
            // reference. try { identityKeyPair_ =
            // crypto::IdentityKeyStore::load(userId, password); } catch (const
            // std::exception&) {
            // // One-time migration of an existing identity; never replace its
            // key pair. bool accepted = false; const auto legacyPassphrase =
            // QInputDialog::getText( this, "Migrate identity", "Enter your
            // previous identity passphrase:", QLineEdit::Password, QString(),
            // &accepted); if (!accepted || legacyPassphrase.isEmpty()) throw
            // std::runtime_error("Identity migration cancelled");
            // identityKeyPair_ = crypto::IdentityKeyStore::load(userId,
            // legacyPassphrase); crypto::IdentityKeyStore::save(userId,
            // *identityKeyPair_, password);
            // }
            identityKeyPair_ = crypto::IdentityKeyStore::load(userId, password);
          } else {
            identityKeyPair_ = crypto::KeyManager::generateKeyPair();
            crypto::IdentityKeyStore::save(userId, *identityKeyPair_, password);
          }
        } catch (const std::exception& e) {
          qWarning() << "Identity initialization failed:" << e.what();
          resetConversations();
          accessToken_.clear();
          refreshToken_.clear();
          loginButton_->setEnabled(true);
          QMessageBox::warning(this, "Identity error",
                               "Unable to initialize identity. Check your "
                               "login password and local identity file.");
          return;
        }
        const auto& pk = identityKeyPair_->publicKey;
        localFingerprintLabel_->setText(
            "Local SHA-256 fingerprint: " +
            crypto::PeerTrustStore::fingerprint(pk));
        const QString encoded = QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(pk.data()), pk.size())
                .toBase64());
        auto* publication = httpClient_.publishIdentity(accessToken_, encoded);
        connect(
            publication, &QNetworkReply::finished, this,
            [this, publication, username]() {
              publication->deleteLater();
              loginButton_->setEnabled(true);
              if (publication->error() != QNetworkReply::NoError ||
                  publication
                          ->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                          .toInt() != 200) {
                resetConversations();
                accessToken_.clear();
                refreshToken_.clear();
                QMessageBox::warning(this, "Identity error",
                                     "Public key publication failed. Check "
                                     "server and existing key.");
                return;
              }
              refreshTimer_->start();

              // Switch to the chat page after successful authentication.
              stackedWidget_->setCurrentWidget(chatPage_);
              resize(900, 600);

              // Update the interface after successful authentication.
              setWindowTitle("SecureChat - Connected as " + username);

              passwordInput_->clear();
              loadConversations();
              conversationListTimer_->start();
            });
      });
}

void MainWindow::refreshAccessToken() {
  if (refreshToken_.isEmpty()) {
    refreshTimer_->stop();
    return;
  }

  refreshTimer_->stop();

  QNetworkReply* reply = httpClient_.refreshToken(refreshToken_);

  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError || status != 200) {
      qWarning() << "Token refresh failed:" << status << reply->errorString();

      stackedWidget_->setCurrentWidget(loginPage_);
      resize(400, 250);
      setWindowTitle("SecureChat");

      resetConversations();

      accessToken_.clear();
      refreshToken_.clear();
      refreshTimer_->stop();

      loginButton_->setText("Login");
      loginButton_->setEnabled(true);
      usernameInput_->setEnabled(true);
      passwordInput_->setEnabled(true);

      reply->deleteLater();
      return;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(reply->readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      qWarning() << "Invalid token refresh response";

      resetConversations();
      accessToken_.clear();
      refreshToken_.clear();
      refreshTimer_->stop();

      stackedWidget_->setCurrentWidget(loginPage_);
      resize(400, 250);
      setWindowTitle("SecureChat");

      reply->deleteLater();
      return;
    }

    const QJsonObject json = document.object();
    const QString newAccessToken = json.value("access_token").toString();
    const QString newRefreshToken = json.value("refresh_token").toString();

    if (newAccessToken.isEmpty() || newRefreshToken.isEmpty()) {
      qWarning() << "Missing refreshed tokens";

      resetConversations();
      accessToken_.clear();
      refreshToken_.clear();
      refreshTimer_->stop();

      stackedWidget_->setCurrentWidget(loginPage_);
      resize(400, 250);
      setWindowTitle("SecureChat");

      reply->deleteLater();
      return;
    }

    // Replace both tokens because refresh tokens are rotated.
    accessToken_ = newAccessToken;
    refreshToken_ = newRefreshToken;

    refreshTimer_->start();

    qDebug() << "Authentication tokens refreshed successfully";

    reply->deleteLater();
  });
}

void MainWindow::resetConversations() {
  conversationListTimer_->stop();
  historyTimer_->stop();
  ++sessionGeneration_;
  ++selectionGeneration_;
  peerKeyPending_ = false;
  fileService_.cancelAll();
  transferLabels_.clear();
  clearCrypto();
  currentUserId_.clear();
  currentConversationId_.clear();
  peerTrustDetails_.clear();
  localFingerprintLabel_->setText("Local fingerprint unavailable.");
  showPeerTrustDetails({});
  securityDialog_->close();
  conversationList_->clear();
  messages_.clear();
  conversationMessageOrder_.clear();
  conversationDrafts_.clear();
  refreshMessages();
  conversationNameInput_->clear();
  conversationParticipantsInput_->clear();
  conversationParticipantsInput_->setEnabled(true);
  createConversationButton_->setEnabled(true);
  conversationNameInput_->setEnabled(true);
  statusBar()->clearMessage();
}

void MainWindow::addConversation(const QString& id, const QString& name,
                                 bool select) {
  QListWidgetItem* item = nullptr;
  for (int row = 0; row < conversationList_->count(); ++row) {
    if (conversationList_->item(row)->data(Qt::UserRole).toString() == id) {
      item = conversationList_->item(row);
      break;
    }
  }
  if (!item) {
    item = new QListWidgetItem(conversationList_);
    item->setData(Qt::UserRole, id);
  }
  item->setText(name.isEmpty() ? "Unnamed conversation" : name);
  item->setToolTip(id);
  if (select) conversationList_->setCurrentItem(item);
  if (item == conversationList_->currentItem())
    conversationTitle_->setText(item->text());
}

void MainWindow::loadConversations(bool background) {
  if (accessToken_.isEmpty() || listingPending_) return;
  listingPending_ = true;
  listingInBackground_ = background;
  listingSession_ = sessionGeneration_;
  if (!background) {
    reloadConversationsButton_->setEnabled(false);
    statusBar()->showMessage("Loading conversations...");
  }
  conversationService_.listConversations(accessToken_);
}

void MainWindow::onCreateConversationClicked() {
  if (accessToken_.isEmpty() || !createConversationButton_->isEnabled()) {
    return;
  }

  const QString name = conversationNameInput_->text().trimmed();

  if (name.isEmpty()) {
    statusBar()->showMessage("A conversation name is required", 5000);
    return;
  }

  QStringList participants;

  const QString input = conversationParticipantsInput_->text();

  for (const QString& username : input.split(',', Qt::SkipEmptyParts)) {
    const QString trimmed = username.trimmed();

    if (!trimmed.isEmpty() && !participants.contains(trimmed)) {
      participants.append(trimmed);
    }
  }

  if (participants.size() > 100) {
    statusBar()->showMessage("Maximum 100 participants", 5000);
    return;
  }

  createConversationButton_->setEnabled(false);
  conversationNameInput_->setEnabled(false);
  conversationParticipantsInput_->setEnabled(false);

  statusBar()->showMessage("Creating conversation...");

  creationSession_ = sessionGeneration_;

  conversationService_.createConversation(accessToken_, name, participants);
}

void MainWindow::onSendClicked() {
  QString message = messageInput_->text().trimmed();

  if (message.isEmpty() || currentConversationId_.isEmpty() ||
      accessToken_.isEmpty() || !sendButton_->isEnabled() ||
      !conversationKeys_.contains(currentConversationId_)) {
    return;
  }

  ChatMessage chatMessage;
  chatMessage.text = message;
  messageInput_->clear();
  sendEncrypted(currentConversationId_, chatMessage, message.toUtf8(),
                network::kTextMessageVersion);
}

void MainWindow::sendEncrypted(const QString& conversationId,
                               ChatMessage chatMessage, QByteArray plaintext,
                               int encryptionVersion) {
  // Generate a unique identifier for the message
  const QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  chatMessage.id = messageId;
  chatMessage.status = MessageStatus::Sending;
  chatMessage.senderId = currentUserId_;
  chatMessage.encryptionVersion = encryptionVersion;

  // Store the message and preserve its display order
  messages_.insert(messageId, chatMessage);
  conversationMessageOrder_[conversationId].append(messageId);

  // Refresh the conversation display
  refreshMessages();

  try {
    const auto keys = conversationKeys_.constFind(conversationId);
    if (keys == conversationKeys_.cend()) {
      throw std::runtime_error("Conversation key unavailable");
    }

    // Encrypt the message before sending it to the network layer
    crypto::EncryptedMessage encryptedMessage =
        crypto::MessageCrypto::encrypt(plaintext.toStdString(), keys->keys.tx);
    // A file message's plaintext holds the file key.
    sodium_memzero(plaintext.data(), plaintext.size());

    // Build the encrypted message request
    network::dto::SendMessageRequest request;

    request.messageId = messageId.toStdString();
    request.encryptedMessage = std::move(encryptedMessage);
    request.encryptionVersion = encryptionVersion;
    request.clientTimestamp = QDateTime::currentDateTimeUtc()
                                  .toString(Qt::ISODateWithMs)
                                  .toStdString();

    // Send the encrypted message asynchronously through the Gateway
    messageService_.sendMessage(conversationId, accessToken_, request);

  } catch (const std::exception& e) {
    sodium_memzero(plaintext.data(), plaintext.size());
    // Update the message status to indicate failure
    updateMessageStatus(messageId, MessageStatus::Failed);
    qWarning() << "Message sending failed:" << e.what();
  }
}

void MainWindow::setComposerEnabled(bool enabled) {
  sendButton_->setEnabled(enabled);
  attachButton_->setEnabled(enabled);
}

void MainWindow::onAttachClicked() {
  if (currentConversationId_.isEmpty() || accessToken_.isEmpty() ||
      !attachButton_->isEnabled() ||
      !conversationKeys_.contains(currentConversationId_)) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(this, "Send a file");
  if (path.isEmpty()) return;

  // Text typed in the message box goes with the file as its caption.
  const QString caption = messageInput_->text().trimmed();
  messageInput_->clear();

  const QString transferId =
      fileService_.upload(currentConversationId_, path, caption);
  transferLabels_.insert(transferId, "Uploading " + QFileInfo(path).fileName());
  statusBar()->showMessage(transferLabels_.value(transferId) + "...");
}

void MainWindow::onMessageLinkClicked(const QUrl& url) {
  if (url.scheme() != "securechat-file" || accessToken_.isEmpty()) return;
  const auto message = messages_.constFind(url.path());
  if (message == messages_.cend() || !message->file) return;
  const network::FileMessage& file = *message->file;

  // The name comes from the sender: strip any path before offering it.
  const QString name = network::safeFileName(file.name);
  const QString directory =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  const QString path = QFileDialog::getSaveFileName(
      this, "Save file", QDir(directory).filePath(name));
  if (path.isEmpty()) return;

  const QString transferId = fileService_.download(file, path);
  transferLabels_.insert(transferId, "Downloading " + name);
  statusBar()->showMessage(transferLabels_.value(transferId) + "...");
}

void MainWindow::connectFileTransfers() {
  connect(&fileService_, &network::FileService::progress, this,
          [this](const QString& id, qint64 done, qint64 total) {
            if (!transferLabels_.contains(id)) return;
            const qint64 percent = total > 0 ? done * 100 / total : 100;
            statusBar()->showMessage(
                QString("%1: %2%").arg(transferLabels_.value(id)).arg(percent));
          });

  connect(&fileService_, &network::FileService::uploadFinished, this,
          [this](const QString& id, const QString& conversationId,
                 const network::FileMessage& file) {
            if (!transferLabels_.remove(id)) return;
            if (!conversationKeys_.contains(conversationId)) {
              // Keys are cleared while the peer identity is re-checked.
              statusBar()->showMessage(
                  "File uploaded but not shared: conversation key unavailable");
              return;
            }
            ChatMessage chatMessage;
            chatMessage.text = file.text;
            chatMessage.file = file;
            sendEncrypted(conversationId, chatMessage,
                          network::FileMessageCodec::serialize(file),
                          network::kFileMessageVersion);
            statusBar()->showMessage("Sent " + network::safeFileName(file.name),
                                     5000);
          });

  connect(&fileService_, &network::FileService::downloadFinished, this,
          [this](const QString& id, const QString& path) {
            if (!transferLabels_.remove(id)) return;
            statusBar()->showMessage("Saved " + path, 10000);
          });

  connect(&fileService_, &network::FileService::transferFailed, this,
          [this](const QString& id, const QString& error) {
            const QString label = transferLabels_.take(id);
            if (label.isEmpty()) return;
            qWarning() << label << "failed:" << error;
            statusBar()->showMessage(label + " failed: " + error);
          });
}

void MainWindow::clearCrypto() {
  if (identityKeyPair_)
    sodium_memzero(identityKeyPair_->privateKey.data(),
                   identityKeyPair_->privateKey.size());
  identityKeyPair_.reset();
  for (auto& entry : conversationKeys_)
    sodium_memzero(&entry.keys, sizeof(entry.keys));
  conversationKeys_.clear();
}

void MainWindow::clearConversationKeys(const QString& id) {
  auto entry = conversationKeys_.find(id);
  if (entry != conversationKeys_.end()) {
    sodium_memzero(&entry->keys, sizeof(entry->keys));
    conversationKeys_.erase(entry);
  }
}

void MainWindow::showPeerTrustDetails(const QString& details) {
  peerTrustLabel_->setText(
      details.isEmpty()
          ? QString("Select a conversation to check peer identity.")
          : details);
  const bool warning = details.startsWith("E2EE ");
  securityWarningLabel_->setText(warning ? details.section('\n', 0, 0)
                                         : QString());
  securityWarningLabel_->setVisible(warning);
}

void MainWindow::loadPeerKey(const QString& id, bool revalidate) {
  if (!identityKeyPair_ || accessToken_.isEmpty() || id.isEmpty() ||
      id != currentConversationId_ || (peerKeyPending_ && !revalidate)) {
    return;
  }
  // Invalidate earlier history replies before requesting a fresh peer key.
  historyTimer_->stop();
  ++selectionGeneration_;
  clearConversationKeys(id);
  peerKeyPending_ = true;
  setComposerEnabled(false);
  if (!peerTrustDetails_.value(id).startsWith("E2EE blocked:")) {
    showPeerTrustDetails("Checking peer identity...");
  }
  const auto session = sessionGeneration_;
  const auto selection = selectionGeneration_;
  auto* reply = httpClient_.getPeerKey(accessToken_, id);
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, id, session, selection]() {
        reply->deleteLater();
        if (session != sessionGeneration_ || selection != selectionGeneration_)
          return;
        peerKeyPending_ = false;
        try {
          const auto obj = QJsonDocument::fromJson(reply->readAll()).object();
          const int httpStatus =
              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                  .toInt();

          if (httpStatus == 409 &&
              obj.value("error").toString() == "TWO_PARTICIPANTS_REQUIRED") {
            loadGroupPeerKeys(id, session, selection);
            return;
          }

          if (reply->error() != QNetworkReply::NoError) {
            throw std::runtime_error("Peer key unavailable");
          }
          if (reply->error() != QNetworkReply::NoError)
            throw std::runtime_error("Peer key unavailable");

          const QUuid peerUuid(obj.value("user_id").toString());
          const auto peer = peerUuid.toString(QUuid::WithoutBraces);
          const auto bytes = QByteArray::fromBase64(
              obj.value("public_key").toString().toLatin1(),
              QByteArray::AbortOnBase64DecodingErrors);
          if (peerUuid.isNull() || peer == currentUserId_ ||
              bytes.size() != crypto_kx_PUBLICKEYBYTES)
            throw std::runtime_error("Invalid peer key");
          crypto::PublicKey pk;
          std::copy(bytes.begin(), bytes.end(), pk.begin());
          const auto trust =
              crypto::PeerTrustStore::checkOrPin(currentUserId_, id, peer, pk);
          using TrustStatus = crypto::PeerTrustStore::Status;
          if (trust.status != TrustStatus::Trusted) {
            const QString reason = trust.status == TrustStatus::PeerMismatch
                                       ? "conversation peer changed"
                                       : "peer key changed";
            const auto warning =
                QString(
                    "E2EE blocked: %1. Verify the peer through a separate "
                    "channel.\n"
                    "Expected: %2\nSHA-256: %3\nPresented: %4\nSHA-256: %5")
                    .arg(reason, trust.expectedPeer, trust.expectedFingerprint,
                         peer, trust.presentedFingerprint);
            peerTrustDetails_.insert(id, warning);
            showPeerTrustDetails(warning);
            return;
          }

          const auto& own = *identityKeyPair_;
          // Stable role ordering gives matching transmit and receive keys.
          auto keys = currentUserId_ < peer
                          ? crypto::KeyManager::deriveClientSessionKeys(
                                own.publicKey, own.privateKey, pk)
                          : crypto::KeyManager::deriveServerSessionKeys(
                                own.publicKey, own.privateKey, pk);
          // Bind keys to the conversation to avoid cross-conversation reuse.
          const auto context = id.toUtf8();
          for (auto* key : {&keys.rx, &keys.tx}) {
            crypto::SessionKey derived{};
            crypto_generichash(
                derived.data(), derived.size(),
                reinterpret_cast<const unsigned char*>(context.constData()),
                context.size(), key->data(), key->size());
            sodium_memzero(key->data(), key->size());
            *key = derived;
            sodium_memzero(derived.data(), derived.size());
          }
          conversationKeys_.insert(id, ConversationKeys{peer, keys});
          sodium_memzero(&keys, sizeof(keys));
          const auto details = QString("Peer UUID: %1\nSHA-256: %2")
                                   .arg(peer, trust.expectedFingerprint);
          peerTrustDetails_.insert(id, details);
          showPeerTrustDetails(details);
          setComposerEnabled(true);
          historyTimer_->start();
          requestActiveHistory();
        } catch (const std::exception& e) {
          if (!peerTrustDetails_.value(id).startsWith("E2EE blocked:")) {
            const auto warning =
                "E2EE unavailable: " + QString::fromUtf8(e.what());
            peerTrustDetails_.insert(id, warning);
            showPeerTrustDetails(warning);
          }
        }
      });
  QTimer::singleShot(15000, reply, [reply] {
    if (reply->isRunning()) reply->abort();
  });
}

void MainWindow::loadGroupPeerKeys(const QString& id, quint64 session,
                                   quint64 selection) {
  peerKeyPending_ = true;
  auto* reply = httpClient_.getPeerKeys(accessToken_, id);

  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, session, selection]() {
        reply->deleteLater();

        if (session != sessionGeneration_ ||
            selection != selectionGeneration_) {
          return;
        }

        peerKeyPending_ = false;

        if (reply->error() != QNetworkReply::NoError) {
          showPeerTrustDetails("E2EE unavailable: Group peer keys unavailable");
          return;
        }

        const auto document = QJsonDocument::fromJson(reply->readAll());
        const auto peers = document.object().value("peers");

        if (!peers.isArray() || peers.toArray().size() != 2) {
          showPeerTrustDetails("E2EE unavailable: Invalid group peer list");
          return;
        }

        showPeerTrustDetails("Group peer keys received. E2EE setup pending.");
      });

  QTimer::singleShot(15000, reply, [reply] {
    if (reply->isRunning()) {
      reply->abort();
    }
  });
}

void MainWindow::requestActiveHistory() {
  if (historyPending_ || !historyTimer_->isActive() || accessToken_.isEmpty()) {
    return;
  }
  const auto id = currentConversationId_;
  const auto session = sessionGeneration_;
  const auto selection = selectionGeneration_;
  historyPending_ = true;
  auto* reply = httpClient_.getMessageHistory(id, accessToken_);

  // Keep the slot occupied until this reply finishes, even after a reset.
  connect(
      reply, &QNetworkReply::finished, this,
      [this, reply, id, session, selection]() {
        historyPending_ = false;
        reply->deleteLater();
        if (session != sessionGeneration_ ||
            selection != selectionGeneration_) {
          requestActiveHistory();
          return;
        }

        const auto status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200 && reply->error() != QNetworkReply::NoError) {
          return;
        }
        applyHistory(id, reply->readAll(), status);
      });

  // Release the polling slot even if the server stops responding.
  QTimer::singleShot(15000, reply, [reply]() {
    if (reply->isRunning()) reply->abort();
  });
}

void MainWindow::applyHistory(const QString& conversationId,
                              const QByteArray& response, int httpStatus) {
  if (httpStatus != 200) {
    qWarning() << "Failed to load history:" << httpStatus;
    return;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(response, &parseError);

  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    qWarning() << "Invalid history JSON:" << parseError.errorString();
    return;
  }

  const QJsonObject root = document.object();

  if (!root.value("messages").isArray()) {
    qWarning() << "Missing messages array";
    return;
  }

  const QJsonArray messages = root.value("messages").toArray();

  bool changed = false;

  for (const QJsonValue& value : messages) {
    if (!value.isObject()) {
      continue;
    }

    const QJsonObject message = value.toObject();

    const QString messageId = message.value("message_id").toString();

    const QString senderId = message.value("user_id").toString();

    const QString senderUsername = message.value("sender_username").toString();

    const QString nonce = message.value("nonce").toString();

    const QString ciphertext = message.value("ciphertext").toString();

    if (messageId.isEmpty()) {
      continue;
    }

    // Keep previously decoded messages.
    if (messages_.contains(messageId) &&
        messages_.value(messageId).text != "[Encrypted message]") {
      continue;
    }

    ChatMessage chatMessage;

    chatMessage.id = messageId;
    chatMessage.text = "[Encrypted message]";
    chatMessage.status = MessageStatus::Sent;

    chatMessage.senderId = senderId;
    chatMessage.senderUsername = senderUsername;
    chatMessage.nonce = nonce;
    chatMessage.ciphertext = ciphertext;

    chatMessage.serverTimestamp = message.value("server_timestamp").toString();

    chatMessage.encryptionVersion =
        message.value("encryption_version").toInt(1);
    const auto keys = conversationKeys_.constFind(conversationId);
    const bool knownVersion =
        chatMessage.encryptionVersion == network::kTextMessageVersion ||
        chatMessage.encryptionVersion == network::kFileMessageVersion;
    if (knownVersion && keys != conversationKeys_.cend() &&
        (senderId == currentUserId_ || senderId == keys->peerId)) {
      try {
        const auto n = QByteArray::fromBase64(
            nonce.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        const auto c = QByteArray::fromBase64(
            ciphertext.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (n.size() != crypto_secretbox_NONCEBYTES ||
            c.size() < crypto_secretbox_MACBYTES)
          throw std::runtime_error("Invalid encrypted payload");
        crypto::EncryptedMessage encrypted;
        std::copy(n.begin(), n.end(), encrypted.nonce.begin());
        encrypted.ciphertext.assign(c.begin(), c.end());
        auto plain = crypto::MessageCrypto::decrypt(
            encrypted,
            senderId == currentUserId_ ? keys->keys.tx : keys->keys.rx);
        if (chatMessage.encryptionVersion == network::kFileMessageVersion) {
          chatMessage.file = network::FileMessageCodec::parse(
              QByteArray::fromStdString(plain));
          chatMessage.text = chatMessage.file ? chatMessage.file->text
                                              : "[Invalid file message]";
        } else {
          chatMessage.text = QString::fromUtf8(plain.data(), plain.size());
        }
        sodium_memzero(plain.data(), plain.size());
      } catch (const std::exception&) {
        chatMessage.text = "[Unable to decrypt]";
      }
    }

    const auto existing = messages_.constFind(messageId);
    changed |=
        existing == messages_.cend() || existing->text != chatMessage.text;
    messages_.insert(messageId, chatMessage);
  }

  QStringList& order = conversationMessageOrder_[conversationId];

  // The server returns newest messages first.
  for (int i = messages.size() - 1; i >= 0; --i) {
    const QJsonObject message = messages.at(i).toObject();

    const QString messageId = message.value("message_id").toString();

    if (messages_.contains(messageId) && !order.contains(messageId)) {
      order.append(messageId);
      changed = true;
    }
  }

  if (changed) {
    refreshMessages();
  }
}

void MainWindow::refreshMessages() {
  messageView_->clear();

  for (const QString& messageId :
       conversationMessageOrder_.value(currentConversationId_)) {
    const ChatMessage& message = messages_.value(messageId);

    QString statusText;

    switch (message.status) {
      case MessageStatus::Sending:
        statusText = "Sending...";
        break;

      case MessageStatus::Sent:
        statusText = "Sent";
        break;

      case MessageStatus::Failed:
        statusText = "Failed";
        break;
    }

    // Render untrusted plaintext as text, never as HTML.
    auto cursor = messageView_->textCursor();
    cursor.movePosition(QTextCursor::End);
    QString senderLabel;
    if (message.senderId == currentUserId_) {
      senderLabel = "You: ";
    } else if (message.senderUsername.isEmpty()) {
      senderLabel = "Peer: ";
    } else {
      senderLabel = message.senderUsername + ": ";
    }

    const QTextCharFormat plain;
    cursor.insertText(senderLabel, plain);
    if (message.file) {
      // A link to this message; clicking it offers to download the file.
      QTextCharFormat link;
      link.setAnchor(true);
      link.setAnchorHref("securechat-file:" + messageId);
      link.setFontUnderline(true);
      link.setForeground(palette().link());
      cursor.insertText(
          QString::fromUtf8("\U0001F4CE %1 (%2)")
              .arg(network::safeFileName(message.file->name),
                   QLocale().formattedDataSize(message.file->size)),
          link);
      if (!message.text.isEmpty()) {
        cursor.insertText(QString::fromUtf8(" \u2014 ") + message.text, plain);
      }
    } else {
      cursor.insertText(message.text, plain);
    }
    cursor.insertText(" [" + statusText + "]\n", plain);
  }
}

void MainWindow::updateMessageStatus(const QString& messageId,
                                     MessageStatus status) {
  // Ignore unknown message identifiers
  auto it = messages_.find(messageId);

  if (it == messages_.end()) {
    return;
  }

  // Update the message status
  it->status = status;

  // Refresh the conversation display
  refreshMessages();
}

}  // namespace ui
