#pragma once

#include <QJsonObject>
#include <QString>
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

        state->flags = static_cast<quint32>(flags);
        state->autodiscoveryFlags = static_cast<quint32>(autodiscoveryFlags);
        state->autoConfigUrl = json[QStringLiteral("auto_config_url")].toString();
        state->proxyServer = json[QStringLiteral("proxy_server")].toString();
        state->proxyBypass = json[QStringLiteral("proxy_bypass")].toString();
        return true;
    }
} // namespace Qv2ray::components::proxy::safety
