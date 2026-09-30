#include "src/components/proxy/ProxyStateSafety.hpp"
#include "src/utils/WindowsCommandLine.hpp"

#include <QFile>
#include <QTemporaryDir>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE("System proxy ownership only restores unchanged owned state")
{
    using namespace Qv2ray::components::proxy::safety;

    SystemProxyState original;
    original.flags = 0x0d;
    original.autodiscoveryFlags = 0x22;
    original.autoConfigUrl = QStringLiteral("https://example.test/proxy.pac");
    original.proxyServer = QStringLiteral("legacy:8080");
    original.proxyBypass = QStringLiteral("localhost;<local>");

    const auto owned = MakeOwnedManualProxyState(original, 0x03, QStringLiteral("127.0.0.1:10809"));
    REQUIRE(owned.flags == 0x03);
    REQUIRE(owned.proxyServer == QStringLiteral("127.0.0.1:10809"));
    REQUIRE(owned.autodiscoveryFlags == original.autodiscoveryFlags);
    REQUIRE(owned.autoConfigUrl == original.autoConfigUrl);
    REQUIRE(owned.proxyBypass == original.proxyBypass);
    REQUIRE(IsStillOwned(owned, owned));

    auto externallyChanged = owned;
    externallyChanged.proxyServer = QStringLiteral("127.0.0.1:9999");
    REQUIRE_FALSE(IsStillOwned(owned, externallyChanged));

    externallyChanged = owned;
    externallyChanged.flags ^= 0x01;
    REQUIRE_FALSE(IsStillOwned(owned, externallyChanged));

    externallyChanged = owned;
    externallyChanged.autoConfigUrl = QStringLiteral("https://other.test/proxy.pac");
    REQUIRE_FALSE(IsStillOwned(owned, externallyChanged));
}

TEST_CASE("External proxy takeover blocks automatic reacquisition until explicit enable")
{
    using namespace Qv2ray::components::proxy::safety;

    ExternalTakeoverLatch latch;
    REQUIRE(latch.AllowsAutomaticSet());
    REQUIRE_FALSE(latch.IsBlocked());

    latch.MarkExternalTakeover();
    REQUIRE_FALSE(latch.AllowsAutomaticSet());
    REQUIRE(latch.IsBlocked());

    latch.AcknowledgeExplicitEnable();
    REQUIRE(latch.AllowsAutomaticSet());
    REQUIRE_FALSE(latch.IsBlocked());
}

TEST_CASE("System proxy configured state requires ownership of every current target")
{
    using namespace Qv2ray::components::proxy::safety;

    const QStringList allTargets{ QString(), QStringLiteral("Corp VPN"), QStringLiteral("Dial-up") };
    REQUIRE(OwnsExactlyTargets(allTargets, allTargets));
    REQUIRE(OwnsExactlyTargets(allTargets, { QStringLiteral("Dial-up"), QString(), QStringLiteral("Corp VPN") }));
    REQUIRE_FALSE(OwnsExactlyTargets(allTargets, { QString(), QStringLiteral("Corp VPN") }));
    REQUIRE_FALSE(OwnsExactlyTargets(allTargets, { QString(), QStringLiteral("Corp VPN"), QStringLiteral("Other") }));
}

TEST_CASE("System proxy snapshots round-trip without losing Windows state")
{
    using namespace Qv2ray::components::proxy::safety;

    SystemProxyState state;
    state.flags = 0xffffffffu;
    state.autodiscoveryFlags = 0x80000001u;
    state.autoConfigUrl = QStringLiteral("https://example.test/pac?q=1");
    state.proxyServer = QStringLiteral("http=127.0.0.1:10809;https=127.0.0.1:10809");
    state.proxyBypass = QStringLiteral("localhost;<local>;*.example.test");

    SystemProxyState restored;
    REQUIRE(SystemProxyStateFromJson(SystemProxyStateToJson(state), &restored));
    REQUIRE(restored == state);

    auto invalid = SystemProxyStateToJson(state);
    invalid.remove(QStringLiteral("proxy_server"));
    REQUIRE_FALSE(SystemProxyStateFromJson(invalid, &restored));

    invalid = SystemProxyStateToJson(state);
    invalid[QStringLiteral("flags")] = 1.5;
    REQUIRE_FALSE(SystemProxyStateFromJson(invalid, &restored));
}

TEST_CASE("Proxy process ownership lock excludes a second live manager")
{
    using namespace Qv2ray::components::proxy::safety;

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto lockPath = directory.filePath(QStringLiteral("proxy-owner.lock"));

    {
        ProcessOwnershipLock first(lockPath);
        ProcessOwnershipLock second(lockPath);
        REQUIRE(first.TryAcquire());
        REQUIRE(first.IsLocked());
        REQUIRE_FALSE(second.TryAcquire());
        REQUIRE_FALSE(second.IsLocked());
    }

    ProcessOwnershipLock afterRelease(lockPath);
    REQUIRE(afterRelease.TryAcquire());
    REQUIRE(afterRelease.IsLocked());
}

TEST_CASE("Proxy recovery location index round-trips independently of the selected config path")
{
    using namespace Qv2ray::components::proxy::safety;

    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto recordPath = directory.filePath(QStringLiteral("proxy-config.json"));
    const auto configPath = QStringLiteral("C:/portable/profile-a/");

    QString loadedPath;
    REQUIRE(ReadConfigPathRecord(recordPath, &loadedPath) == ConfigPathRecordStatus::Missing);
    REQUIRE(WriteConfigPathRecord(recordPath, configPath));
    REQUIRE(ReadConfigPathRecord(recordPath, &loadedPath) == ConfigPathRecordStatus::Loaded);
    REQUIRE(loadedPath == configPath);

    QFile corrupt(recordPath);
    REQUIRE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(corrupt.write("{broken") == 7);
    corrupt.close();
    REQUIRE(ReadConfigPathRecord(recordPath, &loadedPath) == ConfigPathRecordStatus::Error);

    const auto safetyRoot = ProxySafetyDirectoryForBase(QStringLiteral("C:/Users/test/AppData/Local"));
    REQUIRE(safetyRoot.endsWith(QStringLiteral("Qv2ray-Z/proxy-safety")));
    REQUIRE(ProxyRecoveryRecordPathForConfig(configPath).endsWith(QString::fromLatin1(PROXY_RECOVERY_RECORD_FILENAME)));
    REQUIRE(ConfigPathsEquivalent(QStringLiteral("C:/portable/profile-a/"), QStringLiteral("C:/portable/profile-a")));
    REQUIRE_FALSE(ConfigPathsEquivalent(QStringLiteral("C:/portable/profile-a"), QStringLiteral("C:/portable/profile-b")));
#ifdef Q_OS_WIN
    REQUIRE(ConfigPathsEquivalent(QStringLiteral("C:/Portable/Profile-A"), QStringLiteral("c:\\portable\\profile-a\\")));
#endif
}

TEST_CASE("Windows URL protocol command line quotes every argument")
{
    using namespace Qv2ray::utils::windows;

    REQUIRE(BuildUrlProtocolCommand(QStringLiteral("C:\\Program Files\\Qv2ray-Z\\qv2ray.exe")) ==
            QStringLiteral("\"C:\\Program Files\\Qv2ray-Z\\qv2ray.exe\" \"%1\""));

    REQUIRE(QuoteCommandLineArgument(QStringLiteral("C:\\path with space\\")) == QStringLiteral("\"C:\\path with space\\\\\""));
    REQUIRE(QuoteCommandLineArgument(QStringLiteral("a\"b")) == QStringLiteral("\"a\\\"b\""));
}
