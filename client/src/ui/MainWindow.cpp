#include "ui/MainWindow.hpp"

#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStatusBar>
#include <QSplitter>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <exception>
#include <utility>

#include "crypto/MessageCrypto.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "ui/MessageStatusMapper.hpp"

namespace ui {

MainWindow::MainWindow(network::HttpClient& httpClient, QWidget* parent)
    : QMainWindow(parent),
      messageService_(httpClient.baseUrl(), this),
      conversationService_(httpClient.baseUrl(), this),
      httpClient_(httpClient) {
  setWindowTitle("SecureChat");
  resize(900, 600);

  // Generate a temporary encryption key for local integration testing
  randombytes_buf(testSessionKey_.data(), testSessionKey_.size());

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
  createConversationButton_ = new QPushButton("Create conversation", chatPage_);
  createConversationButton_->setObjectName("createConversationButton");

  QHBoxLayout* conversationLayout = new QHBoxLayout();
  conversationLayout->addWidget(conversationNameInput_);
  conversationLayout->addWidget(createConversationButton_);

  messageView_ = new QTextEdit(chatPage_);
  messageView_->setReadOnly(true);

  messageInput_ = new QLineEdit(chatPage_);
  messageInput_->setObjectName("messageInput");
  messageInput_->setPlaceholderText("Type a message...");

  sendButton_ = new QPushButton("Send", chatPage_);
  sendButton_->setObjectName("sendButton");

  QHBoxLayout* inputLayout = new QHBoxLayout();
  inputLayout->addWidget(messageInput_);
  inputLayout->addWidget(sendButton_);

  conversationList_ = new QListWidget(chatPage_);
  conversationList_->setObjectName("conversationList");
  conversationList_->setMinimumWidth(180);
  conversationTitle_ = new QLabel("Select a conversation", chatPage_);
  conversationTitle_->setTextFormat(Qt::PlainText);
  reloadConversationsButton_ = new QPushButton("Refresh conversations", chatPage_);

  auto* sidebar = new QWidget(chatPage_);
  auto* sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->addWidget(new QLabel("Conversations", sidebar));
  sidebarLayout->addWidget(conversationList_);
  sidebarLayout->addWidget(reloadConversationsButton_);
  auto* conversationPanel = new QWidget(chatPage_);
  auto* panelLayout = new QVBoxLayout(conversationPanel);
  panelLayout->addWidget(conversationTitle_);
  panelLayout->addWidget(messageView_);
  panelLayout->addLayout(inputLayout);
  auto* splitter = new QSplitter(chatPage_);
  splitter->addWidget(sidebar);
  splitter->addWidget(conversationPanel);
  splitter->setStretchFactor(1, 1);
  QVBoxLayout* chatLayout = new QVBoxLayout(chatPage_);
  chatLayout->addLayout(conversationLayout);
  chatLayout->addWidget(splitter);
  sendButton_->setEnabled(false);
  messageInput_->setEnabled(false);

  connect(conversationList_, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current, QListWidgetItem*) {
            if (!currentConversationId_.isEmpty()) {
              conversationDrafts_[currentConversationId_] = messageInput_->text();
            }
            currentConversationId_ = current ? current->data(Qt::UserRole).toString() : QString();
            conversationTitle_->setText(current ? current->text() : "Select a conversation");
            messageInput_->setText(conversationDrafts_.value(currentConversationId_));
            sendButton_->setEnabled(current != nullptr);
            messageInput_->setEnabled(current != nullptr);
            refreshMessages();
          });
  connect(reloadConversationsButton_, &QPushButton::clicked, this,
          &MainWindow::loadConversations);

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

  connect(&conversationService_,
          &network::ConversationService::conversationCreated, this,
          [this](const QString& conversationId, const QString& name) {
            createConversationButton_->setEnabled(true);
            conversationNameInput_->setEnabled(true);
            if (accessToken_.isEmpty() || creationSession_ != sessionGeneration_) {
              return;
            }
            addConversation(conversationId, name, true);
            conversationNameInput_->clear();
            statusBar()->showMessage("Conversation created: " + name, 5000);
          });

  connect(&conversationService_,
          &network::ConversationService::conversationCreationFailed, this,
          [this](const QString& error) {
            createConversationButton_->setEnabled(true);
            conversationNameInput_->setEnabled(true);
            if (accessToken_.isEmpty() || creationSession_ != sessionGeneration_) {
              return;
            }
            statusBar()->showMessage("Conversation creation failed: " + error);
          });

  connect(&conversationService_, &network::ConversationService::conversationsLoaded,
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
            if (!conversationList_->currentItem() && conversationList_->count() > 0) {
              conversationList_->setCurrentRow(0);
            }
            statusBar()->showMessage(conversationList_->count() == 0
                                        ? "No conversations yet. Create one to begin."
                                        : "Conversations loaded", 5000);
          });
  connect(&conversationService_, &network::ConversationService::conversationsLoadFailed,
          this, [this](const QString& error) {
            listingPending_ = false;
            reloadConversationsButton_->setEnabled(true);
            if (accessToken_.isEmpty()) return;
            if (listingSession_ != sessionGeneration_) {
              loadConversations();
              return;
            }
            statusBar()->showMessage("Could not load conversations: " + error);
          });
  connect(loginButton_, &QPushButton::clicked, this,
          &MainWindow::onLoginClicked);
  connect(usernameInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onLoginClicked);
  connect(passwordInput_, &QLineEdit::returnPressed, this,
          &MainWindow::onLoginClicked);
}

void MainWindow::onLoginClicked() {
  if (!loginButton_->isEnabled() || stackedWidget_->currentWidget() != loginPage_) {
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

  connect(reply, &QNetworkReply::finished, this, [this, reply, username]() {
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

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
      qWarning() << "Invalid login response";
      reply->deleteLater();
      return;
    }

    const QJsonObject json = document.object();

    const QString accessToken = json.value("access_token").toString();
    const QString refreshToken = json.value("refresh_token").toString();

    if (accessToken.isEmpty() || refreshToken.isEmpty()) {
      qWarning() << "Missing authentication tokens";
      reply->deleteLater();
      return;
    }

    resetConversations();
    accessToken_ = accessToken;
    refreshToken_ = refreshToken;

    refreshTimer_->start();

    // Switch to the chat page after successful authentication.
    stackedWidget_->setCurrentWidget(chatPage_);
    resize(900, 600);

    // Update the interface after successful authentication.
    setWindowTitle("SecureChat - Connected as " + username);

    passwordInput_->clear();
    loadConversations();

    qDebug() << "Login successful for:" << username;

    reply->deleteLater();
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
  ++sessionGeneration_;
  currentConversationId_.clear();
  conversationList_->clear();
  messages_.clear();
  conversationMessageOrder_.clear();
  conversationDrafts_.clear();
  conversationNameInput_->clear();
  statusBar()->clearMessage();
}

void MainWindow::addConversation(const QString& id, const QString& name, bool select) {
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
  if (item == conversationList_->currentItem()) conversationTitle_->setText(item->text());
}

void MainWindow::loadConversations() {
  if (accessToken_.isEmpty() || listingPending_) return;
  listingPending_ = true;
  listingSession_ = sessionGeneration_;
  reloadConversationsButton_->setEnabled(false);
  statusBar()->showMessage("Loading conversations...");
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

  createConversationButton_->setEnabled(false);
  conversationNameInput_->setEnabled(false);
  statusBar()->showMessage("Creating conversation...");

  // Read the current access token on every request, including after rotation.
  creationSession_ = sessionGeneration_;
  conversationService_.createConversation(accessToken_, name);
}

void MainWindow::onSendClicked() {
  QString message = messageInput_->text().trimmed();

  if (message.isEmpty() || currentConversationId_.isEmpty() || accessToken_.isEmpty()) {
    return;
  }

  // Generate a unique identifier for the message
  QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

  // Create the UI message model
  ChatMessage chatMessage{messageId, message, MessageStatus::Sending};

  // Store the message and preserve its display order
  messages_.insert(messageId, chatMessage);
  conversationMessageOrder_[currentConversationId_].append(messageId);

  // Refresh the conversation display
  refreshMessages();

  messageInput_->clear();

  try {
    // Encrypt the message before sending it to the network layer
    crypto::EncryptedMessage encryptedMessage =
        crypto::MessageCrypto::encrypt(message.toStdString(), testSessionKey_);

    // Build the encrypted message request
    network::dto::SendMessageRequest request;

    request.messageId = messageId.toStdString();
    request.encryptedMessage = std::move(encryptedMessage);
    request.encryptionVersion = 1;
    request.clientTimestamp = QDateTime::currentDateTimeUtc()
                                  .toString(Qt::ISODateWithMs)
                                  .toStdString();
  } catch (const std::exception& e) {
    // Update the message status to indicate failure
    updateMessageStatus(messageId, MessageStatus::Failed);
    qWarning() << "Message encryption failed:" << e.what();
    return;
  }
  // Simulate an asynchronous server response
  QTimer::singleShot(1500, this, [this, messageId]() {
    updateMessageStatus(messageId, MessageStatus::Sent);
  });
}

void MainWindow::refreshMessages() {
  messageView_->clear();

  for (const QString& messageId : conversationMessageOrder_.value(currentConversationId_)) {
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

    messageView_->append("You: " + message.text + " [" + statusText + "]");
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
