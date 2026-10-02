#include "Common.hpp"
#include "base/models/QvSettingsObject.hpp"
#include "core/settings/SettingsBackend.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using namespace Qv2ray::base::config;

namespace
{
    constexpr auto ConfigPathEnvironmentVariable = "QV2RAY_CONFIG_PATH";

    class ScopedConfigPath
    {
      public:
        explicit ScopedConfigPath(const QString &path)
            : hadOriginalValue(qEnvironmentVariableIsSet(ConfigPathEnvironmentVariable)), originalValue(qgetenv(ConfigPathEnvironmentVariable))
        {
            qputenv(ConfigPathEnvironmentVariable, path.toUtf8());
        }

        ~ScopedConfigPath()
        {
            if (hadOriginalValue)
                qputenv(ConfigPathEnvironmentVariable, originalValue);
            else
                qunsetenv(ConfigPathEnvironmentVariable);
        }

      private:
        bool hadOriginalValue;
        QByteArray originalValue;
    };

    bool WriteBytes(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(bytes) == bytes.size();
    }

    QByteArray ReadBytes(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
} // namespace

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

TEST_CASE("Retired updater state stays backward compatible without being persisted")
{
    Qv2rayConfigObject config;
    const QJsonObject legacyUpdateConfig{
        { "updateChannel", 1 },
        { "ignoredVersion", "2.7.0-z1" },
    };
    const QJsonObject legacyConfig{
        { "config_version", QV2RAY_CONFIG_VERSION },
        { "updateConfig", legacyUpdateConfig },
        { "logLevel", 2 },
    };

    config.loadJson(legacyConfig);

    REQUIRE(config.logLevel == 2);
    const auto savedConfig = config.toJson();
    REQUIRE_FALSE(savedConfig.contains("updateConfig"));
    REQUIRE(savedConfig["logLevel"].toInt() == 2);
}

TEST_CASE("Malformed configuration remains untouched after initialization failure")
{
    QvTestApplication app;
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    ScopedConfigPath configPath(tempDir.path());

    const auto filePath = QDir(tempDir.path()).filePath("Qv2ray.conf");
    const QByteArray original = R"({"config_version": this-is-not-valid-json, "sentinel": "preserve-me"})";
    REQUIRE(WriteBytes(filePath, original));

    REQUIRE_FALSE(LocateConfiguration());

    GlobalConfig.logLevel = 1;
    SaveGlobalSettings();

    REQUIRE(ReadBytes(filePath) == original);
}

TEST_CASE("Future-version configuration remains untouched after initialization failure")
{
    QvTestApplication app;
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    ScopedConfigPath configPath(tempDir.path());

    const auto filePath = QDir(tempDir.path()).filePath("Qv2ray.conf");
    const QJsonObject futureConfig{
        { "config_version", QV2RAY_CONFIG_VERSION + 1 },
        { "sentinel", "preserve-me" },
    };
    const auto original = QJsonDocument(futureConfig).toJson(QJsonDocument::Compact);
    REQUIRE(WriteBytes(filePath, original));

    REQUIRE_FALSE(LocateConfiguration());

    GlobalConfig.logLevel = 1;
    SaveGlobalSettings();

    REQUIRE(ReadBytes(filePath) == original);
}

TEST_CASE("Successful configuration initialization authorizes later persistence")
{
    QvTestApplication app;
    QTemporaryDir tempDir;
    REQUIRE(tempDir.isValid());
    ScopedConfigPath configPath(tempDir.path());

    const auto filePath = QDir(tempDir.path()).filePath("Qv2ray.conf");
    REQUIRE(LocateConfiguration());

    GlobalConfig.logLevel = 1;
    SaveGlobalSettings();

    const auto saved = QJsonDocument::fromJson(ReadBytes(filePath)).object();
    REQUIRE(saved["config_version"].toInt() == QV2RAY_CONFIG_VERSION);
    REQUIRE(saved["logLevel"].toInt() == 1);
}
