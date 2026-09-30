#pragma once

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
} // namespace Qv2ray::components::proxy::safety
