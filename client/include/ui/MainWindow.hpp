#pragma once

#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>

#include "crypto/CryptoTypes.hpp"
#include "network/MessageService.hpp"
#include "network/dto/ApiResult.hpp"
#include "network/dto/Conversation.hpp"
#include "network/dto/History.hpp"
#include "ui/ChatMessage.hpp"

namespace ui {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onConnectClicked();
    void onSendClicked();

    void onConversationsLoaded(
        const network::dto::ConversationsResult& result);

    void onHistoryLoaded(
        const QString& conversationId,
        const network::dto::HistoryResult& result);

private:
    void buildUi();
    bool prepareSessionKey();
    void startHistoryLoad();
    void appendHistory(const network::dto::HistoryPage& page);
    void refreshMessages();
    void updateMessageStatus(const QString& messageId, MessageStatus status);
    void showApiError(const QString& action,
                      const network::dto::ApiResult& result);

    QTextEdit* messageView_ = nullptr;
    QLineEdit* messageInput_ = nullptr;
    QPushButton* sendButton_ = nullptr;

    QLineEdit* jwtInput_ = nullptr;
    QLineEdit* conversationInput_ = nullptr;
    QLineEdit* passphraseInput_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;

    QHash<QString, ChatMessage> messages_;
    QStringList messageOrder_;

    network::MessageService messageService_;

    QString jwt_;
    QString conversationId_;
    QString passphrase_;
    QString currentUserId_;
    crypto::SessionKey sessionKey_{};
    bool sessionReady_ = false;
};

} // namespace ui
