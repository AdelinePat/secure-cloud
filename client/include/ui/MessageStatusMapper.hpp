#pragma once

#include "ui/MessageStatus.hpp"
#include "network/dto/SendMessageResult.hpp"

namespace ui {

class MessageStatusMapper {
public:
    static MessageStatus fromResult(
        const network::dto::SendMessageResult& result
    );
};

} // namespace ui