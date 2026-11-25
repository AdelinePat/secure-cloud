#pragma once

#include <QString>
#include <optional>

#include "network/FileMessage.hpp"

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

  // Set for file messages (encryption_version 3); `text` is then the caption.
  std::optional<network::FileMessage> file;
};

}  // namespace ui