#pragma once

#include "base/Qv2rayBase.hpp"

namespace Qv2ray::core::connection
{
    inline OUTBOUND PreserveUneditedOutboundFields(const OUTBOUND &original, OUTBOUND edited)
    {
        if (original.value("protocol") != edited.value("protocol"))
            return edited;

        // The outbound editor owns these fields. Preserve everything else so
        // opening and saving a supported outbound cannot silently erase Xray
        // fields that the current UI does not understand. sendThrough is
        // intentionally not editor-owned and therefore retains its original
        // value when one was present.
        const QSet<QString> managedOutboundFields{ "tag", "protocol", "settings", "streamSettings", "mux", QV2RAY_USE_FPROXY_KEY };
        for (auto it = original.constBegin(); it != original.constEnd(); ++it)
        {
            if (!managedOutboundFields.contains(it.key()))
                edited.insert(it.key(), it.value());
        }

        const auto originalStream = original.value("streamSettings").toObject();
        auto editedStream = edited.value("streamSettings").toObject();
        const auto originalNetwork = originalStream.value("network").toString("tcp");
        const auto editedNetwork = editedStream.value("network").toString("tcp");
        if (originalNetwork != editedNetwork)
            return edited;

        // These are the stream-level fields represented by StreamSettingsObject.
        // Preserve only fields outside that model. When the user switches the
        // transport, do not carry unknown settings from the old transport into
        // the new one.
        const QSet<QString> managedStreamFields{ "network",       "security",      "sockopt",       "tlsSettings",  "realitySettings",
                                                 "tcpSettings",   "kcpSettings",   "wsSettings",    "httpSettings", "dsSettings",
                                                 "quicSettings",  "grpcSettings",  "xhttpSettings", "finalmask" };
        for (auto it = originalStream.constBegin(); it != originalStream.constEnd(); ++it)
        {
            if (!managedStreamFields.contains(it.key()))
                editedStream.insert(it.key(), it.value());
        }
        edited["streamSettings"] = editedStream;
        return edited;
    }
} // namespace Qv2ray::core::connection
