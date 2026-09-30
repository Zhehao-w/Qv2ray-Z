#include "base/Qv2rayBase.hpp"
#include "utils/HTTPRequestHelper.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <memory>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

namespace
{
    class TestApplication final : public Qv2rayApplicationInterface
    {
      public:
        void MessageBoxWarn(QWidget *, const QString &, const QString &) override {}
        void MessageBoxInfo(QWidget *, const QString &, const QString &) override {}
        MessageOpt MessageBoxAsk(QWidget *, const QString &, const QString &, const QList<MessageOpt> &) override
        {
            return No;
        }
        void OpenURL(const QString &) override {}
    };

    class LocalHttpServer final : public QObject
    {
      public:
        explicit LocalHttpServer(QObject *parent = nullptr) : QObject(parent)
        {
            QObject::connect(&server, &QTcpServer::newConnection, this, [this]() {
                while (server.hasPendingConnections())
                {
                    auto socket = server.nextPendingConnection();
                    auto requestBuffer = std::make_shared<QByteArray>();
                    QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, requestBuffer]() {
                        requestBuffer->append(socket->readAll());
                        if (!requestBuffer->contains("\r\n\r\n"))
                            return;

                        const auto requestLine = requestBuffer->left(requestBuffer->indexOf("\r\n"));
                        const auto parts = requestLine.split(' ');
                        const auto path = parts.size() >= 2 ? parts.at(1) : QByteArray{};

                        if (path == "/slow")
                        {
                            QTimer::singleShot(500, socket, [socket]() {
                                socket->write("HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nslow");
                                socket->disconnectFromHost();
                            });
                            return;
                        }

                        QByteArray response;
                        if (path == "/ok")
                        {
                            const QByteArray body = "subscription-data";
                            response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                                       "\r\nConnection: close\r\n\r\n" + body;
                        }
                        else if (path == "/error")
                        {
                            const QByteArray body =
                                "vless://one\nvless://two\nvless://three\nvless://four\nvless://five\nvless://six\n";
                            response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: " + QByteArray::number(body.size()) +
                                       "\r\nConnection: close\r\n\r\n" + body;
                        }
                        else if (path == "/large")
                        {
                            const QByteArray body(256, 'x');
                            response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                                       "\r\nConnection: close\r\n\r\n" + body;
                        }
                        else if (path == "/redirect")
                        {
                            response = "HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                        }
                        else
                        {
                            response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                        }

                        socket->write(response);
                        socket->disconnectFromHost();
                    });
                    QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
                }
            });

            REQUIRE(server.listen(QHostAddress::LocalHost, 0));
        }

        QUrl url(const QString &path) const
        {
            return QUrl(QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path));
        }

        quint16 port() const
        {
            return server.serverPort();
        }

        void stop()
        {
            server.close();
        }

      private:
        QTcpServer server;
    };
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    TestApplication qvApp;
    GlobalConfig.networkConfig.proxyType = Qv2rayConfig_Network::QVPROXY_NONE;
    GlobalConfig.networkConfig.userAgent = "Qv2ray-Z HTTP regression";
    return Catch::Session().run(argc, argv);
}

TEST_CASE("Successful HTTP response returns its bounded body")
{
    LocalHttpServer server;
    const auto result = NetworkRequestHelper::HttpGetResult(server.url("/ok"));
    REQUIRE(result.ok());
    REQUIRE(result.httpStatus == 200);
    REQUIRE(result.body == QByteArray("subscription-data"));
}

TEST_CASE("HTTP error body is not exposed as successful subscription data")
{
    LocalHttpServer server;
    const auto result = NetworkRequestHelper::HttpGetResult(server.url("/error"));
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status == NetworkRequestStatus::HttpError);
    REQUIRE(result.httpStatus == 500);
    REQUIRE(result.body.isEmpty());

    const auto legacyBody = NetworkRequestHelper::HttpGet(server.url("/error"));
    REQUIRE(legacyBody.isEmpty());
}

TEST_CASE("Legacy async callbacks complete with an empty body on HTTP failure")
{
    LocalHttpServer server;
    QEventLoop loop;
    bool callbackCalled = false;
    QByteArray callbackBody = "not-empty";

    NetworkRequestHelper::AsyncHttpGet(server.url("/error").toString(), [&](const QByteArray &body) {
        callbackCalled = true;
        callbackBody = body;
        loop.quit();
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(2000);
    loop.exec();
    REQUIRE(callbackCalled);
    REQUIRE(callbackBody.isEmpty());
}

TEST_CASE("Safe redirects are followed")
{
    LocalHttpServer server;
    const auto result = NetworkRequestHelper::HttpGetResult(server.url("/redirect"));
    REQUIRE(result.ok());
    REQUIRE(result.httpStatus == 200);
    REQUIRE(result.body == QByteArray("subscription-data"));
}

TEST_CASE("HTTP requests time out and abort")
{
    LocalHttpServer server;
    NetworkRequestOptions options;
    options.timeoutMs = 50;
    const auto result = NetworkRequestHelper::HttpGetResult(server.url("/slow"), options);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status == NetworkRequestStatus::Timeout);
    REQUIRE(result.body.isEmpty());
}

TEST_CASE("Oversized HTTP responses are rejected without exposing the body")
{
    LocalHttpServer server;
    NetworkRequestOptions options;
    options.maxResponseBytes = 32;
    const auto result = NetworkRequestHelper::HttpGetResult(server.url("/large"), options);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status == NetworkRequestStatus::ResponseTooLarge);
    REQUIRE(result.body.isEmpty());
}

TEST_CASE("Transport failures are distinct from HTTP failures")
{
    NetworkRequestOptions options;
    options.timeoutMs = 500;
    const auto result = NetworkRequestHelper::HttpGetResult(QUrl(QStringLiteral("qv2ray-unsupported://example.invalid/")), options);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status == NetworkRequestStatus::NetworkError);
    REQUIRE(result.httpStatus == 0);
    REQUIRE(result.networkError != QNetworkReply::NoError);
}

TEST_CASE("Async request completes with structured success")
{
    LocalHttpServer server;
    QObject context;
    QEventLoop loop;
    bool callbackCalled = false;

    NetworkRequestHelper::AsyncHttpGetResult(server.url("/ok").toString(), &context, [&](const NetworkRequestResult &result) {
        callbackCalled = true;
        REQUIRE(result.ok());
        REQUIRE(result.body == QByteArray("subscription-data"));
        loop.quit();
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    watchdog.start(2000);
    loop.exec();
    REQUIRE(callbackCalled);
}

TEST_CASE("Destroying async request context suppresses late callbacks")
{
    LocalHttpServer server;
    bool callbackCalled = false;
    auto context = new QObject;

    NetworkRequestOptions options;
    options.timeoutMs = 1000;
    NetworkRequestHelper::AsyncHttpGetResult(server.url("/slow").toString(), context,
                                             [&](const NetworkRequestResult &) { callbackCalled = true; }, options);
    delete context;

    QEventLoop loop;
    QTimer::singleShot(150, &loop, &QEventLoop::quit);
    loop.exec();
    REQUIRE_FALSE(callbackCalled);
}
