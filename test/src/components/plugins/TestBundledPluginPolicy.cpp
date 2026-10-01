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

TEST_CASE("Bundled component search paths use only trusted application and system locations")
{
    qputenv("QV2RAY_RESOURCES_PATH", QByteArray("attacker-controlled-resources"));
    const auto applicationDir = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/qv2ray-policy-test/bin"));
    const auto directories = BundledPluginDirectories(applicationDir);

    // Compare the complete directory list rather than classifying paths by
    // substrings. On Windows QDir::tempPath() normally lives below AppData,
    // which is valid here because it is standing in for applicationDir.
#ifdef Q_OS_WIN
    const QStringList expected{ QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("plugins"))) };
#elif defined(Q_OS_MAC)
    const QStringList expected{ QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("../Resources/plugins"))) };
#else
    const QStringList expected{
        QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("plugins"))),
        QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("../share/qv2ray/plugins"))),
        QStringLiteral("/usr/local/share/qv2ray/plugins"),
        QStringLiteral("/usr/share/qv2ray/plugins"),
    };
#endif

    REQUIRE(directories == expected);
    for (const auto &directory : directories)
        REQUIRE_FALSE(directory.contains(QStringLiteral("attacker-controlled-resources")));
}
