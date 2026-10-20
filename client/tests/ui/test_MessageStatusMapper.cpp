#include <QtTest>

#include "ui/MessageStatusMapper.hpp"

class TestMessageStatusMapper : public QObject {
    Q_OBJECT

private slots:
    void mapsSuccessToSent();
    void mapsErrorsToFailed();
};

void TestMessageStatusMapper::mapsSuccessToSent()
{
    network::dto::SendMessageResult result;

    result.status = network::dto::SendMessageStatus::Success;
    result.httpStatusCode = 200;

    QCOMPARE(
        ui::MessageStatusMapper::fromResult(result),
        ui::MessageStatus::Sent
    );
}

void TestMessageStatusMapper::mapsErrorsToFailed()
{
    using network::dto::SendMessageStatus;

    const SendMessageStatus errors[] = {
        SendMessageStatus::Unauthorized,
        SendMessageStatus::NetworkError,
        SendMessageStatus::ServerError,
        SendMessageStatus::UnknownError
    };

    for (SendMessageStatus status : errors) {

        network::dto::SendMessageResult result;
        result.status = status;

        QCOMPARE(
            ui::MessageStatusMapper::fromResult(result),
            ui::MessageStatus::Failed
        );
    }
}

QTEST_MAIN(TestMessageStatusMapper)

#include "test_MessageStatusMapper.moc"