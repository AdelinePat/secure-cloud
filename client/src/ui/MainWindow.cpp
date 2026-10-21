#include "ui/MainWindow.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUuid>

namespace ui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("SecureChat");
    resize(900, 600);

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

    connect(sendButton_, &QPushButton::clicked,
        this, &MainWindow::onSendClicked);

    connect(messageInput_, &QLineEdit::returnPressed,
        this, &MainWindow::onSendClicked);
}

void MainWindow::onSendClicked()
{
    QString message = messageInput_->text().trimmed();

    if (message.isEmpty()) {
        return;
    }

    // Generate a unique identifier for the message
    QString messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

     // Create the UI message model
    ChatMessage chatMessage{
        messageId,
        message,
        MessageStatus::Sending
    };

     // Store the message using its UUID
    messages_.insert(messageId, chatMessage);

    // Display the message with its initial status
    messageView_->append(
        "You: " + message + " [Sending...]"
    );

    messageInput_->clear();
}

} // namespace ui