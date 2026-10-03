#include "Serialization.hpp"

#include "Generation.hpp"
#include "base/VLESSShareLinkOpaque.hpp"
#include "core/CoreUtils.hpp"
#include "core/handler/ConfigHandler.hpp"

#include <QJsonDocument>
#include <QUrl>
#include <QUrlQuery>

namespace Qv2ray::core::connection
{
    namespace serialization
    {
        QList<std::pair<QString, CONFIGROOT>> ConvertConfigFromString(const QString &link, QString *aliasPrefix, QString *errMessage,
                                                                      QString *newGroup)
        {
            errMessage->clear();

            const static QStringList removedTransports{ "quic", "http", "h2", "h3" };
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
            const auto rejectRemovedTransport = [errMessage](const CONFIGROOT &conf, const QString &protocol)
            {
                const static QStringList removedTransports{ "quic", "http", "h2", "h3" };
                const auto outbounds = conf.value("outbounds").toArray();
                if (outbounds.isEmpty())
                    return false;
                const auto network = outbounds.first().toObject().value("streamSettings").toObject().value("network").toString("tcp");
                if (!removedTransports.contains(network))
                    return false;
                *errMessage = QObject::tr("Unsupported %1 transport: %2").arg(protocol, network);
                return true;
            };
            const auto rejectRemovedKcpUrlFields = [errMessage](const QString &shareLink, const QString &protocol)
            {
                const QUrl url(shareLink);
                const QUrlQuery query(url.query());
                QString network;
                QString headerKey;
                if (protocol == QStringLiteral("VLESS"))
                {
                    network = query.hasQueryItem("type") ? query.queryItemValue("type") : QStringLiteral("tcp");
                    headerKey = QStringLiteral("headerType");
                }
                else
                {
                    for (const auto &component : url.userName().split('+'))
                    {
                        if (component != QStringLiteral("tls"))
                            network = component;
                    }
                    headerKey = QStringLiteral("type");
                }
                if (network != QStringLiteral("kcp"))
                    return false;
                if (query.hasQueryItem("seed"))
                {
                    *errMessage = QObject::tr("Unsupported %1 mKCP seed").arg(protocol);
                    return true;
                }
                if (query.hasQueryItem(headerKey))
                {
                    const auto headerType = query.queryItemValue(headerKey);
                    if (!headerType.isEmpty() && headerType != QStringLiteral("none"))
                    {
                        *errMessage = QObject::tr("Unsupported %1 mKCP header").arg(protocol);
                        return true;
                    }
                }
                return false;
            };
            const auto legacyVMessPayload = [](const QString &legacyLink)
            {
                const auto payload = legacyLink.mid(QStringLiteral("vmess://").size());
                const auto decoded = SafeBase64Decode(payload);
                const auto document = QJsonDocument::fromJson(decoded.toUtf8());
                return document.isObject() ? document.object() : QJsonObject{};
            };
            const auto legacyVMessRequestedTransport = [&legacyVMessPayload](const QString &legacyLink)
            {
                return legacyVMessPayload(legacyLink).value("net").toVariant().toString();
            };
            const auto rejectRemovedLegacyKcpFields = [errMessage, &legacyVMessPayload](const QString &legacyLink)
            {
                const auto payload = legacyVMessPayload(legacyLink);
                if (payload.value("net").toVariant().toString() != QStringLiteral("kcp"))
                    return false;
                if (payload.contains("seed"))
                {
                    *errMessage = QObject::tr("Unsupported VMess mKCP seed");
                    return true;
                }
                if (payload.contains("type"))
                {
                    const auto headerType = payload.value("type").toVariant().toString();
                    if (!headerType.isEmpty() && headerType != QStringLiteral("none"))
                    {
                        *errMessage = QObject::tr("Unsupported VMess mKCP header");
                        return true;
                    }
                }
                return false;
            };

            QList<std::pair<QString, CONFIGROOT>> connectionConf;
            if (link.startsWith("vmess://") && link.contains("@"))
            {
                if (rejectRemovedKcpUrlFields(link, QStringLiteral("VMess")))
                    return {};
                auto conf = vmess_new::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};
                if (rejectRemovedTransport(conf, QStringLiteral("VMess")))
                    return {};
                TLSOptionsFilter(conf);
                connectionConf << std::pair{ *aliasPrefix, conf };
            }
            else if (link.startsWith("vless://"))
            {
                if (rejectRemovedKcpUrlFields(link, QStringLiteral("VLESS")))
                    return {};
                auto conf = vless::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};

                const QUrl inboundUrl(link);
                const QUrlQuery inboundQuery(inboundUrl);
                const auto requestedTransport =
                    inboundQuery.hasQueryItem("type") ? inboundQuery.queryItemValue("type") : QStringLiteral("tcp");
                if (removedTransports.contains(requestedTransport))
                {
                    *errMessage = QObject::tr("Unsupported VLESS transport: %1").arg(requestedTransport);
                    return {};
                }

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
                const auto requestedTransport = legacyVMessRequestedTransport(link);
                if (removedTransports.contains(requestedTransport))
                {
                    *errMessage = QObject::tr("Unsupported VMess transport: %1").arg(requestedTransport);
                    return {};
                }
                if (rejectRemovedLegacyKcpFields(link))
                    return {};

                auto conf = vmess::Deserialize(link, aliasPrefix, errMessage);
                if (!parsedConfigIsUsable(conf))
                    return {};
                if (rejectRemovedTransport(conf, QStringLiteral("VMess")))
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

            const auto network = streamSettings.value("network").toString("tcp");
            const static QStringList removedTransports{ "quic", "http", "h2", "h3" };
            if ((type == "vless" || type == "vmess") && removedTransports.contains(network))
                return type == "vless" ? "(Unsupported VLESS transport)" : "(Unsupported VMess transport)";

            if ((type == "vless" || type == "vmess") && network == QStringLiteral("kcp"))
            {
                const auto kcpSettings = streamSettings.value("kcpSettings").toObject();
                const auto seed = kcpSettings.value("seed").toString();
                const auto headerType = kcpSettings.value("header").toObject().value("type").toString("none");
                if (!seed.isEmpty() || (!headerType.isEmpty() && headerType != QStringLiteral("none")))
                    return type == "vless" ? "(Unsupported VLESS mKCP header/seed)" : "(Unsupported VMess mKCP header/seed)";
            }

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