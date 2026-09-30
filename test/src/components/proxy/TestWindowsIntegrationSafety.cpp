#include "src/components/proxy/ProxyStateSafety.hpp"
#include "src/utils/WindowsCommandLine.hpp"

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

TEST_CASE("Windows URL protocol command line quotes every argument")
{
    using namespace Qv2ray::utils::windows;

    REQUIRE(BuildUrlProtocolCommand(QStringLiteral("C:\\Program Files\\Qv2ray-Z\\qv2ray.exe")) ==
            QStringLiteral("\"C:\\Program Files\\Qv2ray-Z\\qv2ray.exe\" \"%1\""));

    REQUIRE(QuoteCommandLineArgument(QStringLiteral("C:\\path with space\\")) == QStringLiteral("\"C:\\path with space\\\\\""));
    REQUIRE(QuoteCommandLineArgument(QStringLiteral("a\"b")) == QStringLiteral("\"a\\\"b\""));
}
