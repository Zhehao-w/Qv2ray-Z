#include "KernelPathResolver.hpp"

#include <QDir>
#include <QFileInfo>

namespace Qv2ray::core::kernel
{
    namespace
    {
        bool IsValidAssetDirectory(const QString &path)
        {
            const QDir directory(path);
            return !path.isEmpty() && QFileInfo(directory.filePath("geoip.dat")).isFile() && QFileInfo(directory.filePath("geosite.dat")).isFile();
        }
    } // namespace

    KernelPaths ResolveBundledKernelPaths(const QString &configuredExecutable, const QString &configuredAssets, const QString &applicationDir,
                                          const QString &bundledExecutableName)
    {
        if (QFileInfo(configuredExecutable).isFile())
            return { configuredExecutable, configuredAssets, false };

        const auto bundledExecutable = QDir(applicationDir).filePath(bundledExecutableName);
        if (!QFileInfo(bundledExecutable).isFile())
            return { configuredExecutable, configuredAssets, false };

        const auto effectiveAssets = IsValidAssetDirectory(configuredAssets) ? configuredAssets : applicationDir;
        return { bundledExecutable, effectiveAssets, true };
    }
} // namespace Qv2ray::core::kernel
