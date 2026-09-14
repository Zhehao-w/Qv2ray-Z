#include "src/core/kernel/KernelPathResolver.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

namespace
{
    void CreateFile(const QString &path)
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
    }

    void CreateAssets(const QString &directory)
    {
        CreateFile(QDir(directory).filePath("geoip.dat"));
        CreateFile(QDir(directory).filePath("geosite.dat"));
    }
} // namespace

TEST_CASE("Bundled Xray path resolution")
{
    QTemporaryDir temporaryRoot;
    REQUIRE(temporaryRoot.isValid());

    const auto applicationDir = QDir(temporaryRoot.path()).filePath("Qv2ray Z portable");
    REQUIRE(QDir().mkpath(applicationDir));
    const auto bundledExecutable = QDir(applicationDir).filePath("xray.exe");

    SECTION("a valid custom executable wins and is not overwritten")
    {
        const auto customExecutable = QDir(temporaryRoot.path()).filePath("custom-xray.exe");
        CreateFile(customExecutable);
        CreateFile(bundledExecutable);

        const auto paths = Qv2ray::core::kernel::ResolveBundledKernelPaths(customExecutable, "custom-assets", applicationDir, "xray.exe");
        REQUIRE(paths.executable == customExecutable);
        REQUIRE(paths.assets == "custom-assets");
        REQUIRE_FALSE(paths.usesBundledExecutable);
    }

    SECTION("an empty custom path selects the bundled executable and assets")
    {
        CreateFile(bundledExecutable);
        CreateAssets(applicationDir);

        const auto paths = Qv2ray::core::kernel::ResolveBundledKernelPaths({}, {}, applicationDir, "xray.exe");
        REQUIRE(paths.executable == bundledExecutable);
        REQUIRE(paths.assets == applicationDir);
        REQUIRE(paths.usesBundledExecutable);
        REQUIRE(paths.executable.contains("Qv2ray Z portable"));
    }

    SECTION("an invalid custom path deterministically falls back without changing the configured value")
    {
        const auto configured = QDir(temporaryRoot.path()).filePath("removed-custom.exe");
        CreateFile(bundledExecutable);
        CreateAssets(applicationDir);

        const auto paths = Qv2ray::core::kernel::ResolveBundledKernelPaths(configured, {}, applicationDir, "xray.exe");
        REQUIRE(paths.executable == bundledExecutable);
        REQUIRE(paths.assets == applicationDir);
        REQUIRE(paths.usesBundledExecutable);
        REQUIRE(configured.endsWith("removed-custom.exe"));
    }

    SECTION("a missing bundled executable preserves existing missing-kernel behavior")
    {
        const auto configured = QDir(temporaryRoot.path()).filePath("missing-custom.exe");
        const auto paths = Qv2ray::core::kernel::ResolveBundledKernelPaths(configured, {}, applicationDir, "xray.exe");
        REQUIRE(paths.executable == configured);
        REQUIRE(paths.assets.isEmpty());
        REQUIRE_FALSE(paths.usesBundledExecutable);
    }

    SECTION("a valid configured asset directory is preserved with bundled Xray")
    {
        const auto customAssets = QDir(temporaryRoot.path()).filePath("custom assets");
        REQUIRE(QDir().mkpath(customAssets));
        CreateAssets(customAssets);
        CreateFile(bundledExecutable);

        const auto paths = Qv2ray::core::kernel::ResolveBundledKernelPaths({}, customAssets, applicationDir, "xray.exe");
        REQUIRE(paths.executable == bundledExecutable);
        REQUIRE(paths.assets == customAssets);
    }
}
