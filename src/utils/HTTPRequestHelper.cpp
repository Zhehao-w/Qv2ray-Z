#include "HTTPRequestHelper.hpp"

#include "base/Qv2rayBase.hpp"
#include "utils/DiagnosticSafety.hpp"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QPointer>
#include <QSslError>
#include <QTimer>

#include <algorithm>
#include <memory>

#define QV_MODULE_NAME "NetworkCore"

namespace
{
    using namespace Qv2ray::common::network;

    struct RequestState
    {
        QByteArray body;
        bool timedOut = false;
        bool responseTooLarge = false;
        bool sslError = false;
    };

    int boundedTimeout(const NetworkRequestOptions &options)
    {
        return std::max(1, options.timeoutMs);
    }

    qint64 boundedResponseSize(const NetworkRequestOptions &options)
    {
        return std::max<qint64>(1, options.maxResponseBytes);
    }

    void consumeAvailable(QNetworkReply *reply, const std::shared_ptr<RequestState> &state, qint64 maxResponseBytes)
    {
        if (state->responseTooLarge)
        {
            reply->readAll();
            return;
        }

        const auto chunk = reply->readAll();
        if (chunk.size() > maxResponseBytes - state->body.size())
        {
            state->responseTooLarge = true;
            state->body.clear();
            reply->abort();
            return;
        }
        state->body.append(chunk);
    }

    NetworkRequestResult makeResult(QNetworkReply *reply, const std::shared_ptr<RequestState> &state)
    {
        NetworkRequestResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.networkError = reply->error();
        result.errorString = reply->errorString();

        if (state->responseTooLarge)
        {
            result.status = NetworkRequestStatus::ResponseTooLarge;
            result.errorString = QStringLiteral("Response exceeded the configured size limit.");
            return result;
        }
        if (state->timedOut)
        {
            result.status = NetworkRequestStatus::Timeout;
            result.errorString = QStringLiteral("Request timed out.");
            return result;
        }
        if (state->sslError || result.networkError == QNetworkReply::SslHandshakeFailedError)
        {
            result.status = NetworkRequestStatus::SslError;
            return result;
        }
        if (result.httpStatus != 0 && (result.httpStatus < 200 || result.httpStatus >= 300))
        {
            result.status = NetworkRequestStatus::HttpError;
            return result;
        }
        if (result.networkError == QNetworkReply::OperationCanceledError)
        {
            result.status = NetworkRequestStatus::Cancelled;
            return result;
        }
        if (result.networkError != QNetworkReply::NoError)
        {
            result.status = NetworkRequestStatus::NetworkError;
            return result;
        }

        result.status = NetworkRequestStatus::Success;
        result.body = state->body;
        return result;
    }

    void logFailure(const NetworkRequestResult &result)
    {
        if (result.ok())
            return;
        LOG(QString("HTTP request failed: status=%1, networkError=%2, error=%3")
                .arg(result.httpStatus)
                .arg(static_cast<int>(result.networkError))
                .arg(result.errorString));
    }
} // namespace

namespace Qv2ray::common::network
{
    void NetworkRequestHelper::setHeader(QNetworkRequest &request, const QByteArray &key, const QByteArray &value)
    {
        DEBUG("Adding HTTP request header: " + key + ":" + Qv2ray::common::diagnostics::SafeHttpHeaderValueForLog(key, value));
        request.setRawHeader(key, value);
    }

    void NetworkRequestHelper::setAccessManagerAttributes(QNetworkRequest &request, QNetworkAccessManager &accessManager)
    {
        switch (GlobalConfig.networkConfig.proxyType)
        {
            case Qv2rayConfig_Network::QVPROXY_NONE:
            {
                DEBUG("Get without proxy.");
                accessManager.setProxy(QNetworkProxy(QNetworkProxy::ProxyType::NoProxy));
                break;
            }
            case Qv2rayConfig_Network::QVPROXY_SYSTEM:
            {
                accessManager.setProxy(QNetworkProxyFactory::systemProxyForQuery().first());
                break;
            }
            case Qv2rayConfig_Network::QVPROXY_CUSTOM:
            {
                QNetworkProxy p{
                    GlobalConfig.networkConfig.type == "http" ? QNetworkProxy::HttpProxy : QNetworkProxy::Socks5Proxy, //
                    GlobalConfig.networkConfig.address,                                                                //
                    quint16(GlobalConfig.networkConfig.port)                                                           //
                };
                accessManager.setProxy(p);
                break;
            }
            default: Q_UNREACHABLE();
        }

        if (accessManager.proxy().type() == QNetworkProxy::Socks5Proxy)
        {
            DEBUG("Adding HostNameLookupCapability to proxy.");
            auto proxy = accessManager.proxy();
            proxy.setCapabilities(proxy.capabilities() | QNetworkProxy::HostNameLookupCapability);
            accessManager.setProxy(proxy);
        }

        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        // request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
#else
        // request.setAttribute(QNetworkRequest::HTTP2AllowedAttribute, true);
#endif

        auto ua = GlobalConfig.networkConfig.userAgent;
        ua.replace("$VERSION", QV2RAY_VERSION_STRING);
        request.setHeader(QNetworkRequest::KnownHeaders::UserAgentHeader, ua);
    }

    NetworkRequestResult NetworkRequestHelper::HttpGetResult(const QUrl &url, const NetworkRequestOptions &options)
    {
        QNetworkRequest request;
        QNetworkAccessManager accessManager;
        request.setUrl(url);
        setAccessManagerAttributes(request, accessManager);

        const auto maxResponseBytes = boundedResponseSize(options);
        auto reply = accessManager.get(request);
        reply->setReadBufferSize(maxResponseBytes + 1);
        auto state = std::make_shared<RequestState>();

        const auto consume = [reply, state, maxResponseBytes]() { consumeAvailable(reply, state, maxResponseBytes); };
        QObject::connect(reply, &QNetworkReply::readyRead, reply, consume);
        QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, state, maxResponseBytes]() {
            bool validLength = false;
            const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&validLength);
            if (validLength && contentLength > maxResponseBytes)
            {
                state->responseTooLarge = true;
                state->body.clear();
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::sslErrors, reply, [state](const QList<QSslError> &) { state->sslError = true; });

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, reply, [reply, state]() {
            state->timedOut = true;
            reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        timer.start(boundedTimeout(options));
        if (!reply->isFinished())
            loop.exec();
        timer.stop();
        consume();

        const auto result = makeResult(reply, state);
        logFailure(result);
        return result;
    }

    void NetworkRequestHelper::AsyncHttpGetResult(const QString &url, QObject *context, ResultCallback callback,
                                                  const NetworkRequestOptions &options)
    {
        QNetworkRequest request;
        request.setUrl(url);
        auto accessManagerPtr = new QNetworkAccessManager();
        setAccessManagerAttributes(request, *accessManagerPtr);

        const auto maxResponseBytes = boundedResponseSize(options);
        auto reply = accessManagerPtr->get(request);
        reply->setReadBufferSize(maxResponseBytes + 1);
        auto state = std::make_shared<RequestState>();
        const auto consume = [reply, state, maxResponseBytes]() { consumeAvailable(reply, state, maxResponseBytes); };

        QObject::connect(reply, &QNetworkReply::readyRead, accessManagerPtr, consume);
        QObject::connect(reply, &QNetworkReply::metaDataChanged, accessManagerPtr, [reply, state, maxResponseBytes]() {
            bool validLength = false;
            const auto contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&validLength);
            if (validLength && contentLength > maxResponseBytes)
            {
                state->responseTooLarge = true;
                state->body.clear();
                reply->abort();
            }
        });
        QObject::connect(reply, &QNetworkReply::sslErrors, accessManagerPtr, [state](const QList<QSslError> &) { state->sslError = true; });

        auto timeoutTimer = new QTimer(reply);
        timeoutTimer->setSingleShot(true);
        QObject::connect(timeoutTimer, &QTimer::timeout, reply, [reply, state]() {
            state->timedOut = true;
            reply->abort();
        });
        timeoutTimer->start(boundedTimeout(options));

        const QPointer<QObject> contextGuard(context);
        const bool hasContext = context != nullptr;
        if (context != nullptr)
            QObject::connect(context, &QObject::destroyed, reply, [reply]() { reply->abort(); });

        QObject::connect(reply, &QNetworkReply::finished, accessManagerPtr,
                         [accessManagerPtr, reply, timeoutTimer, state, consume, callback = std::move(callback), contextGuard, hasContext]() mutable {
                             timeoutTimer->stop();
                             consume();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                             const bool h2Used = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
#else
                             const bool h2Used = reply->attribute(QNetworkRequest::HTTP2WasUsedAttribute).toBool();
#endif
                             if (h2Used)
                                 DEBUG("HTTP/2 was used.");

                             const auto result = makeResult(reply, state);
                             logFailure(result);
                             if (!hasContext || !contextGuard.isNull())
                                 callback(result);

                             reply->deleteLater();
                             accessManagerPtr->deleteLater();
                         });
    }

    QByteArray NetworkRequestHelper::HttpGet(const QUrl &url)
    {
        const auto result = HttpGetResult(url);
        return result.ok() ? result.body : QByteArray{};
    }

    void NetworkRequestHelper::AsyncHttpGet(const QString &url, std::function<void(const QByteArray &)> funcPtr)
    {
        AsyncHttpGetResult(url, nullptr, [funcPtr = std::move(funcPtr)](const NetworkRequestResult &result) {
            // Preserve legacy completion semantics without exposing HTTP/network error
            // bodies to callers that still accept only a QByteArray.
            funcPtr(result.ok() ? result.body : QByteArray{});
        });
    }

} // namespace Qv2ray::common::network
