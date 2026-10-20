#pragma once

#include <QNetworkReply>

#include "network/dto/SendMessageResult.hpp"

namespace network {

class SendMessageResultMapper {
 public:
  static dto::SendMessageResult fromReply(QNetworkReply* reply);
};

}  // namespace network