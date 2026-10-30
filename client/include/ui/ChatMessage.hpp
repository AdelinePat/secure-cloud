#pragma once

#include <QString>

#include "ui/MessageStatus.hpp"

namespace ui {

struct ChatMessage {
  QString id;
  QString text;
  MessageStatus status = MessageStatus::Sending;

  QString senderId;
  QString nonce;
  QString ciphertext;
  QString serverTimestamp;
  int encryptionVersion = 1;
};

}  // namespace ui