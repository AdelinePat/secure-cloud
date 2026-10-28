#include "network/SendMessageResultMapper.hpp"

#include "network/ApiResultMapper.hpp"

namespace network {

dto::SendMessageResult SendMessageResultMapper::fromReply(
    QNetworkReply* reply) {
  const dto::ApiResult api = ApiResultMapper::fromReply(reply);

  dto::SendMessageResult result;
  result.httpStatusCode = api.httpStatusCode;
  result.errorMessage = api.errorMessage;

  switch (api.status) {
    case dto::RequestStatus::Success:
      result.status = dto::SendMessageStatus::Success;
      result.errorMessage.clear();
      break;
    case dto::RequestStatus::Unauthorized:
      result.status = dto::SendMessageStatus::Unauthorized;
      break;
    case dto::RequestStatus::NetworkError:
      result.status = dto::SendMessageStatus::NetworkError;
      break;
    case dto::RequestStatus::ServerError:
      result.status = dto::SendMessageStatus::ServerError;
      break;
    case dto::RequestStatus::Forbidden:
    case dto::RequestStatus::BadRequest:
    case dto::RequestStatus::NotFound:
    case dto::RequestStatus::Conflict:
    case dto::RequestStatus::UnknownError:
      result.status = dto::SendMessageStatus::UnknownError;
      break;
  }

  return result;
}

}  // namespace network
