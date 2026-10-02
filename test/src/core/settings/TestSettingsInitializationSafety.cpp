#include "Common.hpp"
#include "core/settings/SettingsBackend.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

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
