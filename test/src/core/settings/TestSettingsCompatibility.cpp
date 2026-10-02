#include "base/models/QvSettingsObject.hpp"

#include <QJsonObject>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Qv2ray::base::config;

TEST_CASE("Retired plugin state stays backward compatible without being persisted")
{
    Qv2rayConfigObject config;
    const QJsonObject legacyPluginConfig{
        { "pluginStates", QJsonObject{ { "legacy_external_plugin", false } } },
        { "v2rayIntegration", false },
        { "portAllocationStart", 16001 },
    };
    const QJsonObject legacyConfig{
        { "config_version", QV2RAY_CONFIG_VERSION },
        { "pluginConfig", legacyPluginConfig },
    };

    config.loadJson(legacyConfig);

    REQUIRE_FALSE(config.pluginConfig.v2rayIntegration);
    REQUIRE(config.pluginConfig.portAllocationStart == 16001);

    const auto savedPluginConfig = config.toJson()["pluginConfig"].toObject();
    REQUIRE_FALSE(savedPluginConfig.contains("pluginStates"));
    REQUIRE_FALSE(savedPluginConfig["v2rayIntegration"].toBool());
    REQUIRE(savedPluginConfig["portAllocationStart"].toInt() == 16001);
}

TEST_CASE("Retired UI appearance and language state stays backward compatible without being persisted")
{
    Qv2rayConfigObject config;
    const QJsonObject legacyUiConfig{
        { "theme", "windowsvista" },
        { "language", "zh_CN" },
        { "useDarkTheme", true },
        { "quietMode", false },
        { "useDarkTrayIcon", true },
        { "useGlyphTrayIcon", false },
        { "maximumLogLines", 900 },
    };
    const QJsonObject legacyConfig{
        { "config_version", QV2RAY_CONFIG_VERSION },
        { "uiConfig", legacyUiConfig },
    };

    config.loadJson(legacyConfig);

    REQUIRE_FALSE(config.uiConfig.quietMode);
    REQUIRE(config.uiConfig.useDarkTrayIcon);
    REQUIRE_FALSE(config.uiConfig.useGlyphTrayIcon);
    REQUIRE(config.uiConfig.maximumLogLines == 900);

    const auto savedUiConfig = config.toJson()["uiConfig"].toObject();
    REQUIRE_FALSE(savedUiConfig.contains("theme"));
    REQUIRE_FALSE(savedUiConfig.contains("language"));
    REQUIRE_FALSE(savedUiConfig.contains("useDarkTheme"));
    REQUIRE_FALSE(savedUiConfig["quietMode"].toBool());
    REQUIRE(savedUiConfig["useDarkTrayIcon"].toBool());
    REQUIRE_FALSE(savedUiConfig["useGlyphTrayIcon"].toBool());
    REQUIRE(savedUiConfig["maximumLogLines"].toInt() == 900);
}
