#include "ui/MessageStatusMapper.hpp"

namespace ui {

MessageStatus MessageStatusMapper::fromResult(
    const network::dto::SendMessageResult& result
) {
    using network::dto::SendMessageStatus;

    switch (result.status) {

        case SendMessageStatus::Success:
            return MessageStatus::Sent;

        case SendMessageStatus::Unauthorized:
        case SendMessageStatus::NetworkError:
        case SendMessageStatus::ServerError:
        case SendMessageStatus::UnknownError:
            return MessageStatus::Failed;
    }

    return MessageStatus::Failed;
}

} // namespace ui