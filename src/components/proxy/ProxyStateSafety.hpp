#pragma once

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <memory>
#include <utility>

namespace Qv2ray::components::proxy::safety
{
    constexpr auto PROXY_CONFIG_PATH_RECORD_SCHEMA = 1;
    constexpr auto PROXY_RECOVERY_RECORD_FILENAME = "windows-proxy-recovery.json";

    struct SystemProxyState
    {
        quint32 flags = 0;
        quint32 autodiscoveryFlags = 0;
        QString autoConfigUrl;
        QString proxyServer;
        QString proxyBypass;

        bool operator==(const SystemProxyState &other) const
        {
            return flags == other.flags && autodiscoveryFlags == other.autodiscoveryFlags && autoConfigUrl == other.autoConfigUrl &&
                   proxyServer == other.proxyServer && proxyBypass == other.proxyBypass;
        }

        bool operator!=(const SystemProxyState &other) const
        {
            return !(*this == other);
        }
    };

    class ExternalTakeoverLatch
    {
      public:
        bool AllowsAutomaticSet() const
        {
            return !blocked;
        }

        bool IsBlocked() const
        {
            return blocked;
        }

        void MarkExternalTakeover()
        {
            blocked = true;
        }

        void AcknowledgeExplicitEnable()
        {
            blocked = false;
        }

      private:
        bool blocked = false;
    };

    class ProcessOwnershipLock
    {
      public:
        explicit ProcessOwnershipLock(const QString &path) : lock(path) {}

        bool TryAcquire()
        {
            return lock.isLocked() || lock.tryLock(0);
        }

        bool IsLocked() const
        {
            return lock.isLocked();
        }

      private:
        QLockFile lock;
    };

    enum class ConfigPathRecordStatus
    {
        Missing,
        Loaded,
        Error,
    };

    inline QString ProxySafetyDirectoryForBase(const QString &basePath)
    {
        if (basePath.isEmpty())
            return {};
        return QDir(basePath).filePath(QStringLiteral("Qv2ray-Z/proxy-safety"));
    }

    inline QString ProxySafetyDirectory()
    {
        const auto path = ProxySafetyDirectoryForBase(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
        if (path.isEmpty() || !QDir().mkpath(path))
            return {};
        return path;
    }

    inline bool ConfigPathsEquivalent(const QString &left, const QString &right)
    {
        if (left.isEmpty() || right.isEmpty())
            return left == right;
        const auto normalizedLeft = QDir::cleanPath(QDir::fromNativeSeparators(left));
        const auto normalizedRight = QDir::cleanPath(QDir::fromNativeSeparators(right));
        // Keep case significant even on Windows. NTFS directories can opt into
        // per-directory case sensitivity, so folding case here could collapse
        // two distinct profiles and skip recovery for the real previous owner.
        // A false distinction on a case-insensitive directory is conservative:
        // startup will reconcile/fail closed rather than risk restoring the
        // wrong proxy ownership record.
        return normalizedLeft == normalizedRight;
    }

    inline QString ProxyRecoveryRecordPathForConfig(const QString &configPath)
    {
        if (configPath.isEmpty())
            return {};
        return QDir(configPath).filePath(QString::fromLatin1(PROXY_RECOVERY_RECORD_FILENAME));
    }

    inline QString ProxyProcessLockPath()
    {
        const auto directory = ProxySafetyDirectory();
        return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("windows-proxy-owner.lock"));
    }

    inline QString ProxyConfigPathRecordPath()
    {
        const auto directory = ProxySafetyDirectory();
        return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("windows-proxy-config.json"));
    }

    inline std::unique_ptr<ProcessOwnershipLock> &ProxyProcessLock()
    {
        static std::unique_ptr<ProcessOwnershipLock> lock;
        return lock;
    }

    inline bool EnsureProxyProcessLock()
    {
        auto &lock = ProxyProcessLock();
        if (lock && lock->IsLocked())
            return true;

        const auto path = ProxyProcessLockPath();
        if (path.isEmpty())
            return false;

        auto candidate = std::make_unique<ProcessOwnershipLock>(path);
        if (!candidate->TryAcquire())
            return false;

        lock = std::move(candidate);
        return true;
    }

    inline bool HasProxyProcessLock()
    {
        const auto &lock = ProxyProcessLock();
        return lock && lock->IsLocked();
    }

    inline bool &ProxyAccessAllowedState()
    {
        static bool allowed = false;
        return allowed;
    }

    inline void SetProxyAccessAllowed(bool allowed)
    {
        ProxyAccessAllowedState() = allowed;
    }

    inline bool CanManageSystemProxy()
    {
        return HasProxyProcessLock() && ProxyAccessAllowedState();
    }

    inline ConfigPathRecordStatus ReadConfigPathRecord(const QString &path, QString *configPath)
    {
        if (!configPath || path.isEmpty())
            return ConfigPathRecordStatus::Error;

        configPath->clear();
        if (!QFile::exists(path))
            return ConfigPathRecordStatus::Missing;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return ConfigPathRecordStatus::Error;
        const auto payload = file.readAll();
        const auto readError = file.error();
        file.close();
        if (readError != QFile::NoError)
            return ConfigPathRecordStatus::Error;

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return ConfigPathRecordStatus::Error;

        const auto root = document.object();
        if (root[QStringLiteral("schema")].toInt(-1) != PROXY_CONFIG_PATH_RECORD_SCHEMA ||
            !root[QStringLiteral("config_path")].isString())
            return ConfigPathRecordStatus::Error;

        const auto loadedPath = root[QStringLiteral("config_path")].toString();
        if (loadedPath.isEmpty())
            return ConfigPathRecordStatus::Error;

        *configPath = loadedPath;
        return ConfigPathRecordStatus::Loaded;
    }

    inline bool WriteConfigPathRecord(const QString &path, const QString &configPath)
    {
        if (path.isEmpty() || configPath.isEmpty())
            return false;

        QJsonObject root;
        root[QStringLiteral("schema")] = PROXY_CONFIG_PATH_RECORD_SCHEMA;
        root[QStringLiteral("config_path")] = configPath;
        const auto payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        if (file.write(payload) != payload.size())
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

    inline ConfigPathRecordStatus ReadPreviousProxyConfigPath(QString *configPath)
    {
        const auto path = ProxyConfigPathRecordPath();
        return path.isEmpty() ? ConfigPathRecordStatus::Error : ReadConfigPathRecord(path, configPath);
    }

    inline bool RememberProxyConfigPath(const QString &configPath)
    {
        const auto path = ProxyConfigPathRecordPath();
        return !path.isEmpty() && WriteConfigPathRecord(path, configPath);
    }

    inline bool OwnsExactlyTargets(const QStringList &currentTargets, const QStringList &ownedTargets)
    {
        if (currentTargets.size() != ownedTargets.size())
            return false;

        for (const auto &target : currentTargets)
        {
            if (!ownedTargets.contains(target))
                return false;
        }
        return true;
    }

    inline SystemProxyState MakeOwnedManualProxyState(const SystemProxyState &baseline, quint32 ownedFlags, const QString &proxyServer)
    {
        auto result = baseline;
        result.flags = ownedFlags;
        result.proxyServer = proxyServer;
        return result;
    }

    inline bool IsStillOwned(const SystemProxyState &expectedOwnedState, const SystemProxyState &currentState)
    {
        return expectedOwnedState == currentState;
    }

    inline QJsonObject SystemProxyStateToJson(const SystemProxyState &state)
    {
        return {
            { QStringLiteral("flags"), static_cast<double>(state.flags) },
            { QStringLiteral("autodiscovery_flags"), static_cast<double>(state.autodiscoveryFlags) },
            { QStringLiteral("auto_config_url"), state.autoConfigUrl },
            { QStringLiteral("proxy_server"), state.proxyServer },
            { QStringLiteral("proxy_bypass"), state.proxyBypass },
        };
    }

    inline bool SystemProxyStateFromJson(const QJsonObject &json, SystemProxyState *state)
    {
        if (!state || !json.contains(QStringLiteral("flags")) || !json[QStringLiteral("flags")].isDouble() ||
            !json.contains(QStringLiteral("autodiscovery_flags")) || !json[QStringLiteral("autodiscovery_flags")].isDouble() ||
            !json.contains(QStringLiteral("auto_config_url")) || !json[QStringLiteral("auto_config_url")].isString() ||
            !json.contains(QStringLiteral("proxy_server")) || !json[QStringLiteral("proxy_server")].isString() ||
            !json.contains(QStringLiteral("proxy_bypass")) || !json[QStringLiteral("proxy_bypass")].isString())
            return false;

        const auto flags = json[QStringLiteral("flags")].toDouble();
        const auto autodiscoveryFlags = json[QStringLiteral("autodiscovery_flags")].toDouble();
        if (flags < 0 || flags > 4294967295.0 || autodiscoveryFlags < 0 || autodiscoveryFlags > 4294967295.0)
            return false;

        const auto parsedFlags = static_cast<quint32>(flags);
        const auto parsedAutodiscoveryFlags = static_cast<quint32>(autodiscoveryFlags);
        if (static_cast<double>(parsedFlags) != flags || static_cast<double>(parsedAutodiscoveryFlags) != autodiscoveryFlags)
            return false;

        state->flags = parsedFlags;
        state->autodiscoveryFlags = parsedAutodiscoveryFlags;
        state->autoConfigUrl = json[QStringLiteral("auto_config_url")].toString();
        state->proxyServer = json[QStringLiteral("proxy_server")].toString();
        state->proxyBypass = json[QStringLiteral("proxy_bypass")].toString();
        return true;
    }
} // namespace Qv2ray::components::proxy::safety
