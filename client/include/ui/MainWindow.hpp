#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QHash>

#include "ui/ChatMessage.hpp"

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

};

} // namespace ui