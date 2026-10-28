#pragma once

#include <QByteArray>
#include <QNetworkReply>

#include "network/dto/ApiResult.hpp"

namespace network {

class ApiResultMapper {
 public:
  static dto::ApiResult fromReply(QNetworkReply* reply);

  // Same mapping, but for callers that already consumed the reply body (for
  // example to decode a successful response payload).
  static dto::ApiResult fromReply(QNetworkReply* reply,
                                  const QByteArray& body);
};

}  // namespace network
