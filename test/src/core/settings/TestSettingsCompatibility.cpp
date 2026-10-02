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
