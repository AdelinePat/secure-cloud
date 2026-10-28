#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include "network/dto/Conversation.hpp"

namespace network {

class ConversationCodec {
 public:
  static QString serializeCreate(
      const dto::CreateConversationRequest& request);

  static QVector<dto::ConversationSummary> parseList(
      const QByteArray& json);

  static dto::ConversationDetails parseDetails(const QByteArray& json);
};

}  // namespace network
