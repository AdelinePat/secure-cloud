#pragma once

#include <QMainWindow>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>

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

};

} // namespace ui