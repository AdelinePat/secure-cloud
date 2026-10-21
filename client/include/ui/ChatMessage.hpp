#pragma once

#include <QString>

#include "ui/MessageStatus.hpp"

namespace ui {

struct ChatMessage {
    QString id;
    QString text;
    MessageStatus status = MessageStatus::Sending;
};

} // namespace ui