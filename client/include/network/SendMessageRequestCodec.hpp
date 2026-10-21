#pragma once

#include <string>

#include "network/dto/SendMessageRequest.hpp"

namespace network {

class SendMessageRequestCodec {
 public:
  static std::string serialize(const dto::SendMessageRequest& request);
};

}  // namespace network