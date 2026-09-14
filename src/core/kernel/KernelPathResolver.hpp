#pragma once

#include <QString>

namespace Qv2ray::core::kernel
{
    struct KernelPaths
    {
        QString executable;
        QString assets;
        bool usesBundledExecutable = false;
    };

    KernelPaths ResolveBundledKernelPaths(const QString &configuredExecutable, const QString &configuredAssets, const QString &applicationDir,
                                          const QString &bundledExecutableName);
} // namespace Qv2ray::core::kernel
