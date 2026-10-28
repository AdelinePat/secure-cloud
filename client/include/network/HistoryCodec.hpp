#pragma once

#include <QByteArray>

#include "network/dto/History.hpp"

namespace network {

class HistoryCodec {
 public:
  static dto::HistoryPage parse(const QByteArray& json);
};

}  // namespace network
