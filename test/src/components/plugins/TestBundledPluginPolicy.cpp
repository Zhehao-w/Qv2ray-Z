#include "components/plugins/BundledPluginPolicy.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSet>

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

using namespace Qv2ray::components::plugins::policy;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

TEST_CASE("Only Qv2ray-Z bundled component identities are allowlisted")
{
    const auto &specs = BundledPluginSpecs();
    REQUIRE(specs.size() == 2);

    QSet<QString> fileNames;
    QSet<QString> internalNames;
    for (const auto &spec : specs)
    {
        fileNames.insert(spec.fileName);
        internalNames.insert(spec.internalName);
        REQUIRE(BundledPluginSpecForFileName(spec.fileName) != nullptr);
        REQUIRE(BundledPluginIdentityMatches(spec.fileName, spec.internalName));
        REQUIRE(IsBundledPluginInternalName(spec.internalName));
    }

    REQUIRE(fileNames.size() == 2);
    REQUIRE(internalNames == QSet<QString>{ QStringLiteral("qvplugin_builtin_protocol"), QStringLiteral("builtin_subscription_support") });
}

TEST_CASE("Legacy and arbitrary plugin libraries are rejected by filename")
{
    const QStringList rejected{
        QStringLiteral("evil.dll"),
        QStringLiteral("QvPlugin-SS.dll"),
        QStringLiteral("QvPlugin-Trojan.dll"),
        QStringLiteral("libQvPlugin-SS.so"),
        QStringLiteral("libQvPlugin-Trojan.so"),
        QStringLiteral("QvPlugin-BuiltinProtocolSupport.dll.bak"),
    };

    for (const auto &fileName : rejected)
        REQUIRE(BundledPluginSpecForFileName(fileName) == nullptr);

    REQUIRE_FALSE(IsBundledPluginInternalName(QStringLiteral("qvplugin_ss")));
    REQUIRE_FALSE(IsBundledPluginInternalName(QStringLiteral("third_party_plugin")));

    const auto &specs = BundledPluginSpecs();
    REQUIRE_FALSE(BundledPluginIdentityMatches(specs[0].fileName, specs[1].internalName));
    REQUIRE_FALSE(BundledPluginIdentityMatches(specs[1].fileName, specs[0].internalName));
}

TEST_CASE("Bundled component search paths exclude user configuration and environment resource paths")
{
    qputenv("QV2RAY_RESOURCES_PATH", QByteArray("/tmp/attacker-controlled-resources"));
    const auto directories = BundledPluginDirectories(QStringLiteral("/opt/qv2ray/bin"));

    for (const auto &directory : directories)
    {
        REQUIRE_FALSE(directory.contains(QStringLiteral("attacker-controlled-resources")));
        REQUIRE_FALSE(directory.contains(QStringLiteral("AppData"), Qt::CaseInsensitive));
        REQUIRE_FALSE(directory.contains(QStringLiteral("AppConfig"), Qt::CaseInsensitive));
        REQUIRE_FALSE(directory.contains(QStringLiteral("plugin_settings"), Qt::CaseInsensitive));
    }

#ifdef Q_OS_WIN
    REQUIRE(directories == QStringList{ QDir::cleanPath(QStringLiteral("/opt/qv2ray/bin/plugins")) });
#elif defined(Q_OS_MAC)
    REQUIRE(directories == QStringList{ QDir::cleanPath(QStringLiteral("/opt/qv2ray/Resources/plugins")) });
#else
    REQUIRE(directories.contains(QDir::cleanPath(QStringLiteral("/opt/qv2ray/bin/plugins"))));
    REQUIRE(directories.contains(QDir::cleanPath(QStringLiteral("/opt/qv2ray/share/qv2ray/plugins"))));
    REQUIRE(directories.contains(QStringLiteral("/usr/local/share/qv2ray/plugins")));
    REQUIRE(directories.contains(QStringLiteral("/usr/share/qv2ray/plugins")));
#endif
}
