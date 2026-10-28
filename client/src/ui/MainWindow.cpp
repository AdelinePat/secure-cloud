#include "ui/MainWindow.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include <QDateTime>
#include <QDebug>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include "crypto/ConversationKeyStore.hpp"
#include "crypto/MessageCrypto.hpp"
#include "network/JwtPayload.hpp"
#include "network/dto/SendMessageRequest.hpp"
#include "ui/MessageStatusMapper.hpp"

namespace ui {

namespace {

QUrl gatewayUrl() {
    const QString configured =
        qEnvironmentVariable("SECURECHAT_GATEWAY_URL").trimmed();

    return configured.isEmpty() ? QUrl("http://127.0.0.1:8080")
                                : QUrl(configured);
}

QString statusText(MessageStatus status) {
    switch (status) {
        case MessageStatus::Sending:
            return "Sending...";
        case MessageStatus::Sent:
            return "Sent";
        case MessageStatus::Failed:
            return "Failed";
    }

    return "Unknown";
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      messageService_(gatewayUrl(), this) {
    setWindowTitle("SecureChat");
    resize(900, 600);

    buildUi();

    connect(&messageService_, &network::MessageService::messageSent, this,
            [this](const QString& messageId,
                   const network::dto::SendMessageResult& result) {
                const MessageStatus status =
                    MessageStatusMapper::fromResult(result);

                updateMessageStatus(messageId, status);

                if (status == MessageStatus::Failed) {
                    statusLabel_->setText(
                        "Message failed: " + result.errorMessage);
                } else {
                    statusLabel_->setText("Message sent.");
                }
            });

    connect(&messageService_, &network::MessageService::conversationsLoaded,
            this, &MainWindow::onConversationsLoaded);

    connect(&messageService_, &network::MessageService::historyLoaded, this,
            &MainWindow::onHistoryLoaded);

    // Optional non-interactive startup configuration, useful for local testing
    // and scripted end-to-end runs.
    const QString envJwt = qEnvironmentVariable("SECURECHAT_JWT");
    const QString envConversation =
        qEnvironmentVariable("SECURECHAT_CONVERSATION_ID");
    const QString envPassphrase =
        qEnvironmentVariable("SECURECHAT_PASSPHRASE");

    if (!envJwt.isEmpty()) jwtInput_->setText(envJwt);
    if (!envConversation.isEmpty()) {
        conversationInput_->setText(envConversation);
    }
    if (!envPassphrase.isEmpty()) passphraseInput_->setText(envPassphrase);

    if (!envJwt.isEmpty() && !envPassphrase.isEmpty()) {
        QTimer::singleShot(0, this, &MainWindow::onConnectClicked);
    }
}

void MainWindow::buildUi() {
    QWidget* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* sessionBox = new QGroupBox("Session", centralWidget);
    auto* sessionLayout = new QGridLayout(sessionBox);

    jwtInput_ = new QLineEdit(sessionBox);
    jwtInput_->setPlaceholderText("JWT returned by /secureChat/auth/login");

    conversationInput_ = new QLineEdit(sessionBox);
    conversationInput_->setPlaceholderText("Conversation UUID");

    passphraseInput_ = new QLineEdit(sessionBox);
    passphraseInput_->setPlaceholderText(
        "Shared conversation passphrase (MVP key derivation)");
    passphraseInput_->setEchoMode(QLineEdit::Password);

    connectButton_ = new QPushButton("Connect", sessionBox);

    statusLabel_ = new QLabel(sessionBox);
    statusLabel_->setWordWrap(true);

    sessionLayout->addWidget(new QLabel("JWT:", sessionBox), 0, 0);
    sessionLayout->addWidget(jwtInput_, 0, 1, 1, 2);
    sessionLayout->addWidget(new QLabel("Conversation:", sessionBox), 1, 0);
    sessionLayout->addWidget(conversationInput_, 1, 1, 1, 2);
    sessionLayout->addWidget(new QLabel("Passphrase:", sessionBox), 2, 0);
    sessionLayout->addWidget(passphraseInput_, 2, 1, 1, 2);
    sessionLayout->addWidget(connectButton_, 3, 1);
    sessionLayout->addWidget(statusLabel_, 4, 0, 1, 3);

    messageView_ = new QTextEdit(centralWidget);
    messageView_->setReadOnly(true);

    messageInput_ = new QLineEdit(centralWidget);
    messageInput_->setPlaceholderText("Type a message...");

    sendButton_ = new QPushButton("Send", centralWidget);

    auto* inputLayout = new QHBoxLayout();
    inputLayout->addWidget(messageInput_);
    inputLayout->addWidget(sendButton_);

    auto* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->addWidget(sessionBox);
    mainLayout->addWidget(messageView_);
    mainLayout->addLayout(inputLayout);

    connect(connectButton_, &QPushButton::clicked, this,
            &MainWindow::onConnectClicked);

    connect(sendButton_, &QPushButton::clicked, this,
            &MainWindow::onSendClicked);

    connect(messageInput_, &QLineEdit::returnPressed, this,
            &MainWindow::onSendClicked);
}

void MainWindow::onConnectClicked() {
    const QString jwt = jwtInput_->text().trimmed();
    const QString conversationId = conversationInput_->text().trimmed();
    const QString passphrase = passphraseInput_->text();

    if (jwt.isEmpty() || passphrase.isEmpty()) {
        statusLabel_->setText(
            "Enter a JWT and a passphrase (conversation ID is optional).");
        return;
    }

    jwt_ = jwt;
    conversationId_ = conversationId;
    passphrase_ = passphrase;
    currentUserId_ = network::JwtPayload::subject(jwt_);
    sessionReady_ = false;

    messages_.clear();
    messageOrder_.clear();
    refreshMessages();

    if (!conversationId_.isEmpty()) {
        if (!prepareSessionKey()) return;
        statusLabel_->setText("Loading conversation...");
        startHistoryLoad();
    } else {
        statusLabel_->setText("Loading conversations...");
    }

    // Also validates the token and lets us pick a conversation automatically
    // when the user did not provide an ID.
    messageService_.listConversations(jwt_);
}

bool MainWindow::prepareSessionKey() {
    try {
        sessionKey_ =
            crypto::ConversationKeyStore::derive(conversationId_, passphrase_);
        sessionReady_ = true;
        return true;
    } catch (const std::exception& error) {
        sessionReady_ = false;
        statusLabel_->setText(
            QString("Cannot derive conversation key: %1").arg(error.what()));
        return false;
    }
}

void MainWindow::startHistoryLoad() {
    messageService_.loadHistory(jwt_, conversationId_, {}, 50);
}

void MainWindow::onConversationsLoaded(
    const network::dto::ConversationsResult& result) {
    if (result.meta.status != network::dto::RequestStatus::Success) {
        showApiError("Loading conversations", result.meta);
        return;
    }

    if (conversationId_.isEmpty()) {
        if (result.conversations.isEmpty()) {
            statusLabel_->setText(
                "No conversations are available for this account.");
            return;
        }

        conversationId_ = result.conversations.first().id;
        conversationInput_->setText(conversationId_);

        if (!prepareSessionKey()) return;

        statusLabel_->setText("Loading conversation...");
        startHistoryLoad();
        return;
    }

    statusLabel_->setText(
        QString("Connected. %1 conversation(s) available.")
            .arg(result.conversations.size()));
}

void MainWindow::onHistoryLoaded(
    const QString& conversationId,
    const network::dto::HistoryResult& result) {
    // Ignore a late reply for a conversation the user already left.
    if (conversationId != conversationId_) return;

    if (result.meta.status != network::dto::RequestStatus::Success) {
        showApiError("Loading history", result.meta);
        return;
    }

    appendHistory(result.page);
    refreshMessages();

    statusLabel_->setText(
        QString("Loaded %1 message(s).")
            .arg(result.page.messages.size()));
}

void MainWindow::appendHistory(const network::dto::HistoryPage& page) {
    // History pages arrive newest-first; walk backwards so the visible order
    // stays chronological.
    for (int index = page.messages.size() - 1; index >= 0; --index) {
        const network::dto::ReceivedMessage& received = page.messages.at(index);

        if (messages_.contains(received.messageId)) continue;

        ChatMessage message;
        message.id = received.messageId;
        message.senderId = received.userId;
        message.outgoing =
            !currentUserId_.isEmpty() && received.userId == currentUserId_;
        message.timestamp = received.serverTimestamp;

        try {
            message.text = QString::fromStdString(
                crypto::MessageCrypto::decrypt(received.encryptedMessage,
                                               sessionKey_));
            message.status = MessageStatus::Sent;
        } catch (const std::exception& error) {
            qWarning() << "Unable to decrypt message" << received.messageId
                       << ":" << error.what();
            message.text = "[Unable to decrypt this message]";
            message.status = MessageStatus::Failed;
        }

        messages_.insert(message.id, message);
        messageOrder_.append(message.id);
    }

    std::stable_sort(
        messageOrder_.begin(), messageOrder_.end(),
        [this](const QString& left, const QString& right) {
            return messages_.value(left).timestamp <
                   messages_.value(right).timestamp;
        });
}

void MainWindow::onSendClicked() {
    if (!sessionReady_) {
        statusLabel_->setText("Connect to a conversation first.");
        return;
    }

    const QString text = messageInput_->text().trimmed();
    if (text.isEmpty()) return;

    const QString messageId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);

    ChatMessage message;
    message.id = messageId;
    message.text = text;
    message.status = MessageStatus::Sending;
    message.senderId = currentUserId_;
    message.outgoing = true;
    message.timestamp =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);

    messages_.insert(messageId, message);
    messageOrder_.append(messageId);
    refreshMessages();

    messageInput_->clear();

    try {
        crypto::EncryptedMessage encrypted =
            crypto::MessageCrypto::encrypt(text.toStdString(), sessionKey_);

        network::dto::SendMessageRequest request;
        request.messageId = messageId.toStdString();
        request.encryptedMessage = std::move(encrypted);
        request.encryptionVersion = 1;
        request.clientTimestamp =
            QDateTime::currentDateTimeUtc()
                .toString(Qt::ISODateWithMs)
                .toStdString();

        messageService_.sendMessage(conversationId_, jwt_, request);
    } catch (const std::exception& error) {
        updateMessageStatus(messageId, MessageStatus::Failed);
        statusLabel_->setText(
            QString("Message encryption failed: %1").arg(error.what()));
        qWarning() << "Message encryption failed:" << error.what();
    }
}

void MainWindow::refreshMessages() {
    messageView_->clear();

    for (const QString& messageId : messageOrder_) {
        const ChatMessage& message = messages_.value(messageId);

        QString sender = "Them";
        if (message.outgoing) {
            sender = "You";
        } else if (!message.senderId.isEmpty()) {
            sender = "User " + message.senderId.left(8);
        }

        QString line = sender + ": " + message.text;

        if (message.outgoing) {
            line += " [" + statusText(message.status) + "]";
        }

        messageView_->append(line);
    }
}

void MainWindow::updateMessageStatus(const QString& messageId,
                                     MessageStatus status) {
    auto it = messages_.find(messageId);

    if (it == messages_.end()) {
        return;
    }

    it->status = status;
    refreshMessages();
}

void MainWindow::showApiError(const QString& action,
                              const network::dto::ApiResult& result) {
    QString message = result.errorMessage;
    if (message.isEmpty()) {
        message = QString("HTTP %1").arg(result.httpStatusCode);
    }

    statusLabel_->setText(action + " failed: " + message);
}

}  // namespace ui
