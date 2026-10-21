#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QHash>
#include <QStringList>

#include "ui/ChatMessage.hpp"
#include "network/MessageService.hpp"
#include "crypto/CryptoTypes.hpp"

namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onSendClicked();

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
};

} // namespace ui