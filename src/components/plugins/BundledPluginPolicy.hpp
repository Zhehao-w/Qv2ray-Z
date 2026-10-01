#pragma once

#include <QDir>
#include <QList>
#include <QString>
#include <QStringList>

namespace Qv2ray::components::plugins::policy
{
    struct BundledPluginSpec
    {
        QString fileName;
        QString internalName;
    };

    inline const QList<BundledPluginSpec> &BundledPluginSpecs()
    {
#ifdef Q_OS_WIN
        static const QList<BundledPluginSpec> specs{
            { QStringLiteral("QvPlugin-BuiltinProtocolSupport.dll"), QStringLiteral("qvplugin_builtin_protocol") },
            { QStringLiteral("QvPlugin-BuiltinSubscriptionSupport.dll"), QStringLiteral("builtin_subscription_support") },
        };
#elif defined(Q_OS_LINUX)
        static const QList<BundledPluginSpec> specs{
            { QStringLiteral("libQvPlugin-BuiltinProtocolSupport.so"), QStringLiteral("qvplugin_builtin_protocol") },
            { QStringLiteral("libQvPlugin-BuiltinSubscriptionSupport.so"), QStringLiteral("builtin_subscription_support") },
        };
#else
        static const QList<BundledPluginSpec> specs{};
#endif
        return specs;
    }

    inline const BundledPluginSpec *BundledPluginSpecForFileName(const QString &fileName)
    {
        for (const auto &spec : BundledPluginSpecs())
        {
            if (spec.fileName == fileName)
                return &spec;
        }
        return nullptr;
    }

    inline bool IsBundledPluginInternalName(const QString &internalName)
    {
        for (const auto &spec : BundledPluginSpecs())
        {
            if (spec.internalName == internalName)
                return true;
        }
        return false;
    }

    inline bool BundledPluginIdentityMatches(const QString &fileName, const QString &internalName)
    {
        const auto *spec = BundledPluginSpecForFileName(fileName);
        return spec != nullptr && spec->internalName == internalName;
    }

    inline QStringList BundledPluginDirectories(const QString &applicationDir)
    {
        QStringList directories;
#ifdef Q_OS_WIN
        directories << QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("plugins")));
#elif defined(Q_OS_LINUX)
        // Linux is retained only as an unsupported diagnostic build. Trust only
        // locations tied to the installed application or root-managed system
        // prefixes. User config and environment-provided resource paths are
        // intentionally excluded.
        directories << QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("plugins")));
        directories << QDir::cleanPath(QDir(applicationDir).absoluteFilePath(QStringLiteral("../share/qv2ray/plugins")));
        directories << QStringLiteral("/usr/local/share/qv2ray/plugins");
        directories << QStringLiteral("/usr/share/qv2ray/plugins");
#endif
        directories.removeDuplicates();
        return directories;
    }
} // namespace Qv2ray::components::plugins::policy
