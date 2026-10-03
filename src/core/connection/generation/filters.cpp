#include "core/connection/Generation.hpp"
#include "base/VLESSShareLinkOpaque.hpp"
#include "utils/QvHelpers.hpp"

namespace Qv2ray::core::connection::generation::filters
{
    void OutboundMarkSettingFilter(CONFIGROOT &root, const int mark)
    {
        for (auto i = 0; i < root["outbounds"].toArray().count(); i++)
        {
            QJsonIO::SetValue(root, mark, "outbounds", i, "streamSettings", "sockopt", "mark");
        }
    }

    void RemoveEmptyMuxFilter(CONFIGROOT &root)
    {
        for (auto i = 0; i < root["outbounds"].toArray().count(); i++)
        {
            if (!QJsonIO::GetValue(root, "outbounds", i, "mux", "enabled").toBool(false))
            {
                QJsonIO::SetValue(root, QJsonIO::Undefined, "outbounds", i, "mux");
            }
        }
    }

    void DNSInterceptFilter(CONFIGROOT &root, const bool have_tproxy, const bool have_tproxy_v6, const bool have_socks)
    {
        // Static DNS Objects
        static const QJsonObject dnsOutboundObj{ { "protocol", "dns" }, { "tag", "dns-out" } };

        QJsonArray dnsRouteInTag;

        if (have_tproxy)
            dnsRouteInTag.append("tproxy_IN");
        if (have_tproxy_v6)
            dnsRouteInTag.append("tproxy_IN_V6");
        if (have_socks)
            dnsRouteInTag.append("socks_IN");

        // If no UDP inbound, then DNS outbound is useless.
        if (dnsRouteInTag.isEmpty())
            return;

        const QJsonObject dnsRoutingRuleObj{ { "outboundTag", "dns-out" },
                                            { "port", "53" },
                                            { "type", "field" },
                                            { "inboundTag", dnsRouteInTag } };

        // DNS Outbound
        QJsonIO::SetValue(root, dnsOutboundObj, "outbounds", root["outbounds"].toArray().count());
        // DNS Route
        auto _rules = QJsonIO::GetValue(root, "routing", "rules").toArray();
        _rules.prepend(dnsRoutingRuleObj);
        QJsonIO::SetValue(root, _rules, "routing", "rules");
    }

    void BypassBTFilter(CONFIGROOT &root)
    {
        static const QJsonObject bypassBTRuleObj{ { "protocol", QJsonArray{ "bittorrent" } },
                                                  { "outboundTag", OUTBOUND_TAG_DIRECT },
                                                  { "type", "field" } };
        auto _rules = QJsonIO::GetValue(root, "routing", "rules").toArray();
        _rules.prepend(bypassBTRuleObj);
        QJsonIO::SetValue(root, _rules, "routing", "rules");
    }

    void mKCPSeedFilter(CONFIGROOT &root)
    {
        for (auto i = 0; i < root["outbounds"].toArray().count(); i++)
        {
            const auto seedItem = QJsonIO::GetValue(root, "outbounds", i, "streamSettings", "kcpSettings", "seed");
            bool shouldProcess = !seedItem.isNull() && !seedItem.isUndefined();
            bool isEmptySeed = seedItem.toString().isEmpty();
            if (shouldProcess && isEmptySeed)
                QJsonIO::SetValue(root, QJsonIO::Undefined, "outbounds", i, "streamSettings", "kcpSettings", "seed");
        }
    }

    void FillupTagsFilter(CONFIGROOT &root, const QString &subKey)
    {
        for (auto i = 0; i < root[subKey].toArray().count(); i++)
        {
            if (QJsonIO::GetValue(root, subKey, i, "tag").toString().isEmpty())
            {
                const auto tag = GenerateRandomString(8);
                QJsonIO::SetValue(root, tag, subKey, i, "tag");
            }

            // Xray v26.3.27 rejects legacy mKCP header and seed fields.
            // Strip them only from the final runtime copy for both inbounds and
            // outbounds so persisted configurations keep their compatibility data.
            const auto streamSettings = QJsonIO::GetValue(root, subKey, i, "streamSettings").toObject();
            const auto network = streamSettings.value("network").toString();
            const auto method = streamSettings.value("method").toString();
            if (network == QStringLiteral("kcp") || network == QStringLiteral("mkcp") || method == QStringLiteral("kcp") ||
                method == QStringLiteral("mkcp"))
            {
                QJsonIO::SetValue(root, QJsonIO::Undefined, subKey, i, "streamSettings", "kcpSettings", "header");
                QJsonIO::SetValue(root, QJsonIO::Undefined, subKey, i, "streamSettings", "kcpSettings", "seed");
            }

            if (subKey == QStringLiteral("outbounds"))
            {
                // Opaque VLESS share-link query items are Qv2ray-Z persistence
                // metadata, not Xray streamSettings. The final runtime pass reaches
                // both simple and complex outbounds, so strip the reserved key here
                // before validation/launch while leaving stored/editor data intact.
                if (QJsonIO::GetValue(root, subKey, i, "protocol").toString() == QStringLiteral("vless"))
                {
                    QJsonIO::SetValue(root, QJsonIO::Undefined, subKey, i, "streamSettings",
                                      Qv2ray::base::vless_share::OpaqueQueryMetadataKey());
                }
            }
        }
    }

} // namespace Qv2ray::core::connection::generation::filters
