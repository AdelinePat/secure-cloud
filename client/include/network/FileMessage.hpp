#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <optional>

#include "crypto/FileCrypto.hpp"

namespace network {

// encryption_version of a chat message tells the client what its decrypted
// plaintext is. Both are encrypted the same way (secretbox, conversation key).
inline constexpr int kTextMessageVersion = 2;  // UTF-8 text
inline constexpr int kFileMessageVersion = 3;  // FileMessage JSON

// Everything a participant needs to download and decrypt a shared file.
// Travels only inside an end-to-end encrypted chat message.
struct FileMessage {
  QString fileId;
  crypto::FileKey key{};
  crypto::FileHeader header{};
  QString name;  // as chosen by the sender: never use it as a path as-is
  qint64 size = 0;  // plaintext bytes
  QString mime;
  QString text;  // optional caption
};

class FileMessageCodec {
 public:
  static QByteArray serialize(const FileMessage& message);

  // nullopt if the JSON is not a valid file message.
  static std::optional<FileMessage> parse(const QByteArray& json);
};

// A file name that is safe to offer in a save dialog: no directories, no
// control characters, never empty.
QString safeFileName(const QString& name);

}  // namespace network
