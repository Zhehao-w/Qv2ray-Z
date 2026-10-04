#pragma once

#include "base/Qv2rayBase.hpp"
#include "base/VLESSShareLinkOpaque.hpp"

namespace Qv2ray::core::connection
{
    inline CONFIGROOT ReplaceEditedSingleOutbound(const CONFIGROOT &originalRoot, const OUTBOUND &editedOutbound)
    {
        auto result = originalRoot;
        auto outbounds = result.value(QStringLiteral("outbounds")).toArray();
        if (outbounds.isEmpty())
            outbounds.append(editedOutbound);
        else
            outbounds[0] = editedOutbound;
        result.insert(QStringLiteral("outbounds"), outbounds);
        return result;
    }

    inline void PreserveUnknownObjectFields(const QJsonObject &original, QJsonObject &edited, const QSet<QString> &managedFields)
    {
        for (auto it = original.constBegin(); it != original.constEnd(); ++it)
        {
            if (!managedFields.contains(it.key()))
                edited.insert(it.key(), it.value());
        }
    }

    inline void PreserveUnknownStreamObjectFields(const QJsonObject &originalStream, QJsonObject &editedStream, const QString &field,
                                                  const QSet<QString> &managedFields)
    {
        const auto originalObject = originalStream.value(field).toObject();
        if (originalObject.isEmpty())
            return;

        auto editedObject = editedStream.value(field).toObject();
        PreserveUnknownObjectFields(originalObject, editedObject, managedFields);
        editedStream.insert(field, editedObject);
    }

    inline void PreserveUnknownNestedObjectFields(const QJsonObject &originalParent, QJsonObject &editedParent, const QString &field,
                                                  const QSet<QString> &managedFields)
    {
        if (!originalParent.value(field).isObject())
            return;

        const auto originalObject = originalParent.value(field).toObject();
        auto editedObject = editedParent.value(field).toObject();
        PreserveUnknownObjectFields(originalObject, editedObject, managedFields);
        editedParent.insert(field, editedObject);
    }

    inline void PreserveTypedTransportHeaderFields(const QJsonObject &originalStream, QJsonObject &editedStream, const QString &settingsField,
                                                   const bool tcpHeader)
    {
        if (!originalStream.value(settingsField).isObject())
            return;

        const auto originalSettings = originalStream.value(settingsField).toObject();
        auto editedSettings = editedStream.value(settingsField).toObject();
        if (!originalSettings.value("header").isObject())
            return;

        const auto originalHeader = originalSettings.value("header").toObject();
        auto editedHeader = editedSettings.value("header").toObject();
        const auto originalHeaderType = originalHeader.value("type").toString(QStringLiteral("none"));
        const auto editedHeaderType = editedHeader.value("type").toString(QStringLiteral("none"));
        if (originalHeaderType != editedHeaderType)
            return;

        if (tcpHeader)
        {
            PreserveUnknownObjectFields(originalHeader, editedHeader, { "type", "request", "response" });
            PreserveUnknownNestedObjectFields(originalHeader, editedHeader, QStringLiteral("request"),
                                              { "version", "method", "path", "headers" });
            PreserveUnknownNestedObjectFields(originalHeader, editedHeader, QStringLiteral("response"),
                                              { "version", "status", "reason", "headers" });
        }
        else
        {
            PreserveUnknownObjectFields(originalHeader, editedHeader, { "type" });
        }

        editedSettings.insert(QStringLiteral("header"), editedHeader);
        editedStream.insert(settingsField, editedSettings);
    }

    inline QString NormalizedStreamSecurity(const QJsonObject &stream)
    {
        const auto security = stream.value("security").toString();
        return security.isEmpty() ? QStringLiteral("none") : security;
    }

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

        const auto originalSecurity = NormalizedStreamSecurity(originalStream);
        const auto editedSecurity = NormalizedStreamSecurity(editedStream);

        // These are the stream-level fields represented by StreamSettingsObject.
        // Preserve only fields outside that model. When the user switches the
        // transport, do not carry unknown settings from the old transport into
        // the new one. Opaque share-link query metadata is also discarded on an
        // explicit security switch because an unknown item may be security-specific.
        const QSet<QString> managedStreamFields{ "network",       "security",      "sockopt",       "tlsSettings",  "realitySettings",
                                                 "tcpSettings",   "kcpSettings",   "wsSettings",    "httpSettings", "dsSettings",
                                                 "quicSettings",  "grpcSettings",  "xhttpSettings", "finalmask" };
        for (auto it = originalStream.constBegin(); it != originalStream.constEnd(); ++it)
        {
            if (managedStreamFields.contains(it.key()))
                continue;
            if (it.key() == Qv2ray::base::vless_share::OpaqueQueryMetadataKey() && originalSecurity != editedSecurity)
                continue;
            editedStream.insert(it.key(), it.value());
        }

        // StreamSettingsObject uses typed objects for several Xray settings.
        // Unknown keys inside those objects would otherwise be discarded by
        // fromJson() -> toJson(). Keep only keys outside the fields that the
        // current editor owns, so explicit user edits and resets remain
        // authoritative. TLS certificates are intentionally not managed here:
        // the current certificate editor is inactive, so preserve that array
        // wholesale instead of rebuilding it through the narrower typed model.
        PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("sockopt"),
                                          { "mark", "tcpFastOpen", "tproxy", "tcpKeepAliveInterval" });

        if (originalSecurity == editedSecurity)
        {
            if (editedSecurity == QStringLiteral("tls"))
            {
                PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("tlsSettings"),
                                                  { "serverName", "fingerprint", "enableSessionResumption", "disableSystemRoot", "alpn",
                                                    "pinnedPeerCertSha256" });
            }
            else if (editedSecurity == QStringLiteral("reality"))
            {
                PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("realitySettings"),
                                                  { "serverName", "fingerprint", "password", "shortId", "mldsa65Verify", "spiderX" });
            }
        }

        if (editedNetwork == QStringLiteral("tcp"))
        {
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("tcpSettings"), { "header" });
            PreserveTypedTransportHeaderFields(originalStream, editedStream, QStringLiteral("tcpSettings"), true);
        }
        else if (editedNetwork == QStringLiteral("http"))
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("httpSettings"), { "host", "path", "method", "headers" });
        else if (editedNetwork == QStringLiteral("ws"))
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("wsSettings"),
                                              { "path", "headers", "maxEarlyData", "useBrowserForwarding", "earlyDataHeaderName" });
        else if (editedNetwork == QStringLiteral("kcp"))
        {
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("kcpSettings"),
                                              { "mtu", "tti", "uplinkCapacity", "downlinkCapacity", "congestion", "readBufferSize",
                                                "writeBufferSize", "header", "seed" });
            PreserveTypedTransportHeaderFields(originalStream, editedStream, QStringLiteral("kcpSettings"), false);
        }
        else if (editedNetwork == QStringLiteral("ds"))
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("dsSettings"), { "path" });
        else if (editedNetwork == QStringLiteral("quic"))
        {
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("quicSettings"), { "security", "key", "header" });
            PreserveTypedTransportHeaderFields(originalStream, editedStream, QStringLiteral("quicSettings"), false);
        }
        else if (editedNetwork == QStringLiteral("grpc"))
            PreserveUnknownStreamObjectFields(originalStream, editedStream, QStringLiteral("grpcSettings"), { "serviceName", "multiMode" });
        // xhttpSettings and finalmask are already stored as opaque QJsonObject
        // values and therefore retain unknown nested fields without help here.

        edited["streamSettings"] = editedStream;
        return edited;
    }
} // namespace Qv2ray::core::connection
