#pragma once

#include <QString>

#include "ui/MessageStatus.hpp"

namespace ui {

struct ChatMessage {
    QString id;
    QString text;
    MessageStatus status = MessageStatus::Sending;
    QString senderId;
    bool outgoing = true;
    QString timestamp;
};

} // namespace ui
