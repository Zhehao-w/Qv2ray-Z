#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <functional>

namespace Qv2ray::common::network
{
    enum class NetworkRequestStatus
    {
        Success,
        HttpError,
        NetworkError,
        SslError,
        Timeout,
        Cancelled,
        ResponseTooLarge
    };

    struct NetworkRequestOptions
    {
        int timeoutMs = 15000;
        qint64 maxResponseBytes = 4 * 1024 * 1024;
    };

    struct NetworkRequestResult
    {
        NetworkRequestStatus status = NetworkRequestStatus::NetworkError;
        int httpStatus = 0;
        QByteArray body;
        QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
        QString errorString;

        bool ok() const
        {
            return status == NetworkRequestStatus::Success;
        }
    };

    class NetworkRequestHelper : QObject
    {
        Q_OBJECT
        explicit NetworkRequestHelper(QObject *parent) : QObject(parent){};
        ~NetworkRequestHelper(){};

      public:
        using ResultCallback = std::function<void(const NetworkRequestResult &)>;

        static NetworkRequestResult HttpGetResult(const QUrl &url, const NetworkRequestOptions &options = {});
        static void AsyncHttpGetResult(const QString &url, QObject *context, ResultCallback callback,
                                       const NetworkRequestOptions &options = {});

        // Compatibility wrappers. Failed requests never expose their response body to
        // legacy callers; migrate security-sensitive callers to the structured APIs.
        static void AsyncHttpGet(const QString &url, std::function<void(const QByteArray &)> funcPtr);
        static QByteArray HttpGet(const QUrl &url);

      private:
        static void setAccessManagerAttributes(QNetworkRequest &request, QNetworkAccessManager &accessManager);
        static void setHeader(QNetworkRequest &request, const QByteArray &key, const QByteArray &value);
    };
} // namespace Qv2ray::common::network

using namespace Qv2ray::common::network;
