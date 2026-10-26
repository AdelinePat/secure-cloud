#include "ui/MainWindow.hpp"

#include <QDateTime>
#include <QDebug>
#include <QHBoxLayout>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <exception>
#include <utility>

#include "crypto/MessageCrypto.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "ui/MessageStatusMapper.hpp"

namespace ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      messageService_(QUrl("http://127.0.0.1:8080"), this) {
  setWindowTitle("SecureChat");
  resize(900, 600);

  // Generate a temporary encryption key for local integration testing
  randombytes_buf(testSessionKey_.data(), testSessionKey_.size());

  // Create the central widget
  QWidget* centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  // Create the message display area
  messageView_ = new QTextEdit(centralWidget);
  messageView_->setReadOnly(true);

  // Create the message input
  messageInput_ = new QLineEdit(centralWidget);
  messageInput_->setPlaceholderText("Type a message...");

  // Create the send button
  sendButton_ = new QPushButton("Send", centralWidget);

  // Create the horizontal input layout
  QHBoxLayout* inputLayout = new QHBoxLayout();
  inputLayout->addWidget(messageInput_);
  inputLayout->addWidget(sendButton_);

  // Create the main vertical layout
  QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
  mainLayout->addWidget(messageView_);
  mainLayout->addLayout(inputLayout);

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
  request.clientTimestamp =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
}
catch (const std::exception& e) {
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