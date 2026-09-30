#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace Qv2ray::components::proxy::safety
{
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
