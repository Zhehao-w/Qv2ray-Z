#include "utils/DiagnosticSafety.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSet>

#include <csignal>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

using namespace Qv2ray::common::diagnostics;
using Qv2ray::base::config::Qv2rayConfigObject;
using Qv2ray::base::config::Qv2rayConfig_Network;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(QStringLiteral("diagnostic-test"));
    return Catch::Session().run(argc, argv);
}

TEST_CASE("Diagnostic reports contain only allowlisted configuration metadata")
{
    Qv2rayConfigObject config;
    const QString sentinel = QStringLiteral("SUPER-SECRET-DIAGNOSTIC-SENTINEL");

    config.networkConfig.proxyType = Qv2rayConfig_Network::QVPROXY_CUSTOM;
    config.networkConfig.address = sentinel;
    config.networkConfig.userAgent = sentinel;
    config.networkConfig.latencyRealPingTestURL = sentinel;
    config.inboundConfig.socksSettings.account.user = sentinel;
    config.inboundConfig.socksSettings.account.pass = sentinel;
    config.inboundConfig.httpSettings.account.user = sentinel;
    config.inboundConfig.httpSettings.account.pass = sentinel;
    config.kernelConfig.v2CorePath_linux = sentinel;
    config.kernelConfig.v2AssetsPath_linux = sentinel;
    config.kernelConfig.v2CorePath_macx = sentinel;
    config.kernelConfig.v2AssetsPath_macx = sentinel;
    config.kernelConfig.v2CorePath_win = sentinel;
    config.kernelConfig.v2AssetsPath_win = sentinel;

    const auto info = BuildSafeDiagnosticInfo(&config, 2, 3);
    const auto serialized = QJsonDocument(info).toJson(QJsonDocument::Compact);
    const auto report = BuildSafeDiagnosticReport(&config, 2, 3).toUtf8();

    REQUIRE_FALSE(serialized.contains(sentinel.toUtf8()));
    REQUIRE_FALSE(report.contains(sentinel.toUtf8()));
    REQUIRE(info.value(QStringLiteral("configLoaded")).toBool());
    REQUIRE(info.value(QStringLiteral("networkProxyMode")).toInt() == static_cast<int>(Qv2rayConfig_Network::QVPROXY_CUSTOM));
    REQUIRE(info.value(QStringLiteral("activeKernelCount")).toInt() == 2);
    REQUIRE(info.value(QStringLiteral("pluginCount")).toInt() == 3);

    const QSet<QString> expectedKeys{
        QStringLiteral("reportVersion"),     QStringLiteral("application"),      QStringLiteral("version"),
        QStringLiteral("qtVersion"),         QStringLiteral("operatingSystem"), QStringLiteral("cpuArchitecture"),
        QStringLiteral("activeKernelCount"), QStringLiteral("pluginCount"),      QStringLiteral("configLoaded"),
        QStringLiteral("configVersion"),     QStringLiteral("networkProxyMode"), QStringLiteral("kernelApiEnabled")
    };
    QSet<QString> actualKeys;
    for (const auto &key : info.keys())
        actualKeys.insert(key);
    REQUIRE(actualKeys == expectedKeys);
}

TEST_CASE("HTTP header diagnostics redact every value")
{
    REQUIRE(SafeHttpHeaderValueForLog("Authorization", "Bearer secret") == QByteArray("<redacted>"));
    REQUIRE(SafeHttpHeaderValueForLog("Proxy-Authorization", "Basic secret") == QByteArray("<redacted>"));
    REQUIRE(SafeHttpHeaderValueForLog("Cookie", "session=secret") == QByteArray("<redacted>"));
    REQUIRE(SafeHttpHeaderValueForLog("Host", "private.example") == QByteArray("<redacted>"));
    REQUIRE(SafeHttpHeaderValueForLog("User-Agent", "Qv2ray-Z test") == QByteArray("<redacted>"));
    REQUIRE(SafeHttpHeaderValueForLog("Content-Type", "application/json") == QByteArray("<redacted>"));
}

TEST_CASE("Signal classification excludes uncatchable signals and queues control actions")
{
#ifndef Q_OS_WIN
    const auto fatalSignals = FatalSignals();
    REQUIRE(fatalSignals.contains(SIGABRT));
    REQUIRE(fatalSignals.contains(SIGSEGV));

    const auto controlSignals = ControlSignals();
    REQUIRE(controlSignals.contains(SIGTERM));
    REQUIRE(controlSignals.contains(SIGHUP));
    REQUIRE(controlSignals.contains(SIGUSR1));
    REQUIRE(controlSignals.contains(SIGUSR2));
    REQUIRE_FALSE(fatalSignals.contains(SIGKILL));
    REQUIRE_FALSE(controlSignals.contains(SIGKILL));
#else
    REQUIRE(FatalSignals().isEmpty());
    REQUIRE(ControlSignals().isEmpty());
#endif
}
