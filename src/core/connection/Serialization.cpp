#include "Serialization.hpp"

#include "Generation.hpp"
#include "base/VLESSShareLinkOpaque.hpp"
#include "core/handler/ConfigHandler.hpp"

namespace Qv2ray::core::connection
{
    namespace serialization
    {
        QList<std::pair<QString, CONFIGROOT>> ConvertConfigFromString(const QString &link, QString *aliasPrefix, QString *errMessage,
                                                                      QString *newGroup)
        {
            errMessage->clear();

            const auto TLSOptionsFilter = [](QJsonObject &conf)
            {
                const auto disableSystemRoot = GlobalConfig.advancedConfig.disableSystemRoot;
                QJsonIO::SetValue(conf, disableSystemRoot, { "outbounds", 0, "streamSettings", "tlsSettings", "disableSystemRoot" });
            };
            const auto parsedConfigIsUsable = [errMessage](const CONFIGROOT &conf)
            {
                if (!errMessage->isEmpty())
                    return false;
                if (conf.isEmpty())
                {
                    *errMessage = QObject::tr("Share link parser returned an empty configuration.");
                    return false;
                }
                return true;
            };
            const auto rejectedRemovedVlessTransport = [errMessage](const CONFIGROOT &conf)
            {
                const auto outbounds = conf.value("outbounds").toArray();
                if (outbounds.isEmpty())
                    return false;
                const auto network = outbounds.first().toObject().value("streamSettings").toObject().value("network").toString("tcp");
                if (network != "quic")
                    return false;
                *errMessage = QObject::tr("Unsupported VLESS transport: %1").arg(network);
                return true;
            };

            QList<std::pair<QString, CONFIGROOT>> connectionConf;
            if (link.startsWith("vmess://") && link.contains("@"))
            {
                auto conf = vmess_new::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};
                TLSOptionsFilter(conf);
                connectionConf << std::pair{ *aliasPrefix, conf };
            }
            else if (link.startsWith("vless://"))
            {
                auto conf = vless::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};
                if (rejectedRemovedVlessTransport(conf))
                    return {};

                const auto opaqueQueryItems = Qv2ray::base::vless_share::ExtractOpaqueQueryItems(link);
                if (!opaqueQueryItems.isEmpty())
                {
                    QJsonIO::SetValue(conf, opaqueQueryItems, "outbounds", 0, "streamSettings",
                                      Qv2ray::base::vless_share::OpaqueQueryMetadataKey());
                }

                TLSOptionsFilter(conf);
                connectionConf << std::pair{ *aliasPrefix, conf };
            }
            else if (link.startsWith("vmess://"))
            {
                auto conf = vmess::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};
                TLSOptionsFilter(conf);
                connectionConf << std::pair{ *aliasPrefix, conf };
            }
            else if (link.startsWith("ss://") && !link.contains("plugin="))
            {
                auto conf = ss::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};
                connectionConf << std::pair{ *aliasPrefix, conf };
            }
            else if (link.startsWith("ssd://"))
            {
                QStringList errMessageList;
                connectionConf << ssd::Deserialize(link, newGroup, &errMessageList);
                *errMessage = errMessageList.join(NEWLINE);
            }
            else
            {
                bool ok = false;
                const auto configs = PluginHost->TryDeserializeShareLink(link, aliasPrefix, errMessage, newGroup, ok);
                if (ok)
                {
                    errMessage->clear();
                    if (configs.isEmpty())
                    {
                        *errMessage = QObject::tr("Share link parser returned no configurations.");
                        return {};
                    }
                    for (const auto &[_alias, _protocol, _outbound] : configs)
                    {
                        CONFIGROOT root;
                        auto outbound = GenerateOutboundEntry(OUTBOUND_TAG_PROXY, _protocol, OUTBOUNDSETTING(_outbound), {});
                        QJsonIO::SetValue(root, outbound, "outbounds", 0);
                        connectionConf << std::pair{ _alias, root };
                    }
                }
                else if (errMessage->isEmpty())
                {
                    *errMessage = QObject::tr("Unsupported share link format.");
                }
            }

            return connectionConf;
        }

        const QString ConvertConfigToString(const ConnectionGroupPair &identifier, bool isSip002)
        {
            auto alias = GetDisplayName(identifier.connectionId);
            if (IsComplexConfig(identifier.connectionId))
            {
                return QV2RAY_SERIALIZATION_COMPLEX_CONFIG_PLACEHOLDER;
            }
            auto server = ConnectionManager->GetConnectionRoot(identifier.connectionId);
            return ConvertConfigToString(alias, GetDisplayName(identifier.groupId), server, isSip002);
        }

        const QString ConvertConfigToString(const QString &alias, const QString &groupName, const CONFIGROOT &server, bool isSip002)
        {
            const auto outbound = OUTBOUND(server["outbounds"].toArray().first().toObject());
            const auto type = outbound["protocol"].toString();
            const auto settings = outbound["settings"].toObject();
            const auto streamSettings = outbound["streamSettings"].toObject();

            QString sharelink;

            if (type.isEmpty())
            {
                return "";
            }

            if (type == "vless" && streamSettings.value("network").toString("tcp") == "quic")
                return "(Unsupported VLESS transport)";

            if (type == "vmess")
            {
                const auto vmessServer = VMessServerObject::fromJson(settings["vnext"].toArray().first().toObject());
                const auto transport = StreamSettingsObject::fromJson(streamSettings);
                if (GlobalConfig.uiConfig.useOldShareLinkFormat)
                    sharelink = vmess::Serialize(transport, vmessServer, alias);
                else
                    sharelink = vmess_new::Serialize(transport, vmessServer, alias);
            }
            else if (type == "shadowsocks")
            {
                auto ssServer = ShadowSocksServerObject::fromJson(settings["servers"].toArray().first().toObject());
                sharelink = ss::Serialize(ssServer, alias, isSip002);
            }
            else
            {
                bool ok = false;
                sharelink = PluginHost->SerializeOutbound(type, settings, streamSettings, alias, groupName, &ok);
                Q_UNUSED(ok)
                if (type == "vless")
                {
                    sharelink = Qv2ray::base::vless_share::AppendOpaqueQueryItems(
                        sharelink, streamSettings.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray());
                }
            }

            return sharelink;
        }

    } // namespace serialization
} // namespace Qv2ray::core::connection
