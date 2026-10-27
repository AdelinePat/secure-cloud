#include "ui/MainWindow.hpp"

#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
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
      messageService_(QUrl("http://127.0.0.1:8080"), this),
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
  usernameInput_->setPlaceholderText("Username");

  passwordInput_ = new QLineEdit(loginPage_);
  passwordInput_->setPlaceholderText("Password");
  passwordInput_->setEchoMode(QLineEdit::Password);

  loginButton_ = new QPushButton("Login", loginPage_);

  QVBoxLayout* loginLayout = new QVBoxLayout(loginPage_);
  loginLayout->addStretch();
  loginLayout->addWidget(usernameInput_);
  loginLayout->addWidget(passwordInput_);
  loginLayout->addWidget(loginButton_);
  loginLayout->addStretch();

  // Create the chat page.
  chatPage_ = new QWidget(stackedWidget_);

  messageView_ = new QTextEdit(chatPage_);
  messageView_->setReadOnly(true);

  messageInput_ = new QLineEdit(chatPage_);
  messageInput_->setPlaceholderText("Type a message...");

  sendButton_ = new QPushButton("Send", chatPage_);

  QHBoxLayout* inputLayout = new QHBoxLayout();
  inputLayout->addWidget(messageInput_);
  inputLayout->addWidget(sendButton_);

  QVBoxLayout* chatLayout = new QVBoxLayout(chatPage_);
  chatLayout->addWidget(messageView_);
  chatLayout->addLayout(inputLayout);

  // Register both pages.
  stackedWidget_->addWidget(loginPage_);
  stackedWidget_->addWidget(chatPage_);

  // Show the login page when the application starts.
  stackedWidget_->setCurrentWidget(loginPage_);
  resize(400, 250);

  // Initialize the token refresh timer.
  refreshTimer_ = new QTimer(this);
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

  connect(loginButton_, &QPushButton::clicked, this,
          &MainWindow::onLoginClicked);
}

void MainWindow::onLoginClicked() {
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

    accessToken_ = accessToken;
    refreshToken_ = refreshToken;

    refreshTimer_->start();

    // Switch to the chat page after successful authentication.
    stackedWidget_->setCurrentWidget(chatPage_);
    resize(900, 600);

    // Update the interface after successful authentication.
    setWindowTitle("SecureChat - Connected as " + username);

    passwordInput_->clear();

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

void MainWindow::onSendClicked() {
  QString message = messageInput_->text().trimmed();

  if (message.isEmpty()) {
    return;
  }

  // Generate a unique identifier for the message
  QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

  // Create the UI message model
  ChatMessage chatMessage{messageId, message, MessageStatus::Sending};

  // Store the message and preserve its display order
  messages_.insert(messageId, chatMessage);
  messageOrder_.append(messageId);

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

  for (const QString& messageId : messageOrder_) {
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