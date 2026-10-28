#include "network/MessageService.hpp"

#include <exception>

#include <QNetworkReply>

#include "network/ApiResultMapper.hpp"
#include "network/ConversationCodec.hpp"
#include "network/HistoryCodec.hpp"
#include "network/SendMessageResultMapper.hpp"

namespace network {

namespace {

// Parse a response body only when the request itself succeeded. A malformed
// payload from the server is reported as a server-side failure rather than
// silently producing an empty result.
template <typename Parse>
bool parsePayload(const QByteArray& body, dto::ApiResult& meta, Parse parse) {
  if (meta.status != dto::RequestStatus::Success) return false;

  try {
    parse();
    return true;
  } catch (const std::exception& error) {
    meta.status = dto::RequestStatus::ServerError;
    meta.errorMessage =
        QString("Invalid response from server: %1").arg(error.what());
    return false;
  }
}

}  // namespace

MessageService::MessageService(const QUrl& baseUrl, QObject* parent)
    : QObject(parent),
      httpClient_(baseUrl, this)
{
    qRegisterMetaType<dto::SendMessageResult>();
    qRegisterMetaType<dto::ConversationResult>();
    qRegisterMetaType<dto::ConversationsResult>();
    qRegisterMetaType<dto::HistoryResult>();
}

void MessageService::sendMessage(
    const QString& conversationId,
    const QString& jwt,
    const dto::SendMessageRequest& request)
{
    QNetworkReply* reply =
        httpClient_.sendMessage(conversationId, jwt, request);

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, messageId = QString::fromStdString(request.messageId)]() {

            dto::SendMessageResult result =
                SendMessageResultMapper::fromReply(reply);

            emit messageSent(messageId, result);

            reply->deleteLater();
        }
    );
}

void MessageService::listConversations(const QString& jwt)
{
    QNetworkReply* reply = httpClient_.listConversations(jwt);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();

        dto::ConversationsResult result;
        result.meta = ApiResultMapper::fromReply(reply, body);

        parsePayload(body, result.meta, [&]() {
            result.conversations = ConversationCodec::parseList(body);
        });

        emit conversationsLoaded(result);
        reply->deleteLater();
    });
}

void MessageService::createConversation(
    const QString& jwt,
    const dto::CreateConversationRequest& request)
{
    QNetworkReply* reply =
        httpClient_.createConversation(jwt, request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();

        dto::ConversationResult result;
        result.meta = ApiResultMapper::fromReply(reply, body);

        parsePayload(body, result.meta, [&]() {
            result.conversation = ConversationCodec::parseDetails(body);
        });

        emit conversationCreated(result);
        reply->deleteLater();
    });
}

void MessageService::loadHistory(
    const QString& jwt,
    const QString& conversationId,
    const QString& before,
    int limit)
{
    QNetworkReply* reply =
        httpClient_.getHistory(jwt, conversationId, before, limit);

    connect(
        reply,
        &QNetworkReply::finished,
        this,
        [this, reply, conversationId]() {
            const QByteArray body = reply->readAll();

            dto::HistoryResult result;
            result.meta = ApiResultMapper::fromReply(reply, body);

            parsePayload(body, result.meta, [&]() {
                result.page = HistoryCodec::parse(body);
            });

            emit historyLoaded(conversationId, result);
            reply->deleteLater();
        }
    );
}

}  // namespace network
