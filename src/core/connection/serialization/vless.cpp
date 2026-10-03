#include "core/CoreUtils.hpp"
#include "core/connection/Generation.hpp"
#include "core/connection/Serialization.hpp"
#include "utils/QvHelpers.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

#define QV_MODULE_NAME "VLESSImporter"

namespace
{
    bool IsHexDigit(const QChar ch)
    {
        const auto value = ch.unicode();
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
    }

    bool DecodeQueryItemStrict(const QString &link, const QString &key, bool *found, QString *decoded)
    {
        *found = false;
        decoded->clear();

        const auto queryStart = link.indexOf('?');
        if (queryStart < 0)
            return true;
        const auto fragmentStart = link.indexOf('#', queryStart + 1);
        const auto rawQuery = fragmentStart < 0 ? link.mid(queryStart + 1) : link.mid(queryStart + 1, fragmentStart - queryStart - 1);

        for (const auto &item : rawQuery.split('&'))
        {
            const auto equals = item.indexOf('=');
            const auto rawKey = equals < 0 ? item : item.left(equals);
            if (rawKey != key)
                continue;
            if (*found)
                return false;

            *found = true;
            const auto rawValue = equals < 0 ? QString{} : item.mid(equals + 1);
            for (int i = 0; i < rawValue.size(); ++i)
            {
                if (rawValue.at(i) != '%')
                    continue;
                if (i + 2 >= rawValue.size() || !IsHexDigit(rawValue.at(i + 1)) || !IsHexDigit(rawValue.at(i + 2)))
                    return false;
                i += 2;
            }
            *decoded = QUrl::fromPercentEncoding(rawValue.toUtf8());
        }
        return true;
    }
}

namespace Qv2ray::core::connection
{
    namespace serialization::vless
    {
        CONFIGROOT Deserialize(const QString &str, QString *alias, QString *errMessage)
        {
            // must start with vless://
            if (!str.startsWith("vless://"))
            {
                *errMessage = QObject::tr("VLESS link should start with vless://");
                return CONFIGROOT();
            }

            // parse url
            QUrl url(str);
            if (!url.isValid())
            {
                *errMessage = QObject::tr("link parse failed: %1").arg(url.errorString());
                return CONFIGROOT();
            }

            // fetch host
            const auto hostRaw = url.host();
            if (hostRaw.isEmpty())
            {
                *errMessage = QObject::tr("empty host");
                return CONFIGROOT();
            }
            const auto host = (hostRaw.startsWith('[') && hostRaw.endsWith(']')) ? hostRaw.mid(1, hostRaw.length() - 2) : hostRaw;

            // fetch port
            const auto port = url.port();
            if (port == -1)
            {
                *errMessage = QObject::tr("missing port");
                return CONFIGROOT();
            }

            // fetch remarks
            const auto remarks = url.fragment();
            if (!remarks.isEmpty())
            {
                *alias = remarks;
            }

            // fetch uuid
            const auto uuid = url.userInfo();
            if (uuid.isEmpty())
            {
                *errMessage = QObject::tr("missing uuid");
                return CONFIGROOT();
            }

            // initialize QJsonObject with basic info
            QJsonObject outbound;
            QJsonObject stream;

            QJsonIO::SetValue(outbound, "vless", "protocol");
            QJsonIO::SetValue(outbound, host, { "settings", "vnext", 0, "address" });
            QJsonIO::SetValue(outbound, port, { "settings", "vnext", 0, "port" });
            QJsonIO::SetValue(outbound, uuid, { "settings", "vnext", 0, "users", 0, "id" });

            // parse query
            QUrlQuery query(url.query());

            // handle type
            const auto hasType = query.hasQueryItem("type");
            const auto linkType = hasType ? query.queryItemValue("type") : "tcp";
            // Xray calls this transport RAW, but continues to accept `tcp` as
            // its compatibility alias. Keep Qv2ray storage and share links on
            // `tcp`, while accepting either spelling on import.
            const auto type = linkType == "raw" ? QStringLiteral("tcp") : linkType;
            const static QStringList supportedTransports{ "tcp", "http", "ws", "kcp", "quic", "grpc", "xhttp" };
            if (!supportedTransports.contains(type))
            {
                *errMessage = QObject::tr("Unsupported VLESS transport: %1").arg(type);
                return CONFIGROOT();
            }
            if (type != "tcp")
                QJsonIO::SetValue(stream, type, "network");

            // handle encryption
            const auto hasEncryption = query.hasQueryItem("encryption");
            const auto encryption = hasEncryption ? query.queryItemValue("encryption") : "none";
            QJsonIO::SetValue(outbound, encryption, { "settings", "vnext", 0, "users", 0, "encryption" });

            // type-wise settings
            if (type == "kcp")
            {
                const auto hasSeed = query.hasQueryItem("seed");
                if (hasSeed)
                    QJsonIO::SetValue(stream, query.queryItemValue("seed"), { "kcpSettings", "seed" });

                const auto hasHeaderType = query.hasQueryItem("headerType");
                const auto headerType = hasHeaderType ? query.queryItemValue("headerType") : "none";
                if (headerType != "none")
                    QJsonIO::SetValue(stream, headerType, { "kcpSettings", "header", "type" });
            }
            else if (type == "http")
            {
                const auto hasPath = query.hasQueryItem("path");
                const auto path = hasPath ? QUrl::fromPercentEncoding(query.queryItemValue("path").toUtf8()) : "/";
                if (path != "/")
                    QJsonIO::SetValue(stream, path, { "httpSettings", "path" });

                const auto hasHost = query.hasQueryItem("host");
                if (hasHost)
                {
                    const auto hosts = QJsonArray::fromStringList(query.queryItemValue("host").split(","));
                    QJsonIO::SetValue(stream, hosts, { "httpSettings", "host" });
                }
            }
            else if (type == "ws")
            {
                const auto hasPath = query.hasQueryItem("path");
                const auto path = hasPath ? QUrl::fromPercentEncoding(query.queryItemValue("path").toUtf8()) : "/";
                if (path != "/")
                    QJsonIO::SetValue(stream, path, { "wsSettings", "path" });

                const auto hasHost = query.hasQueryItem("host");
                if (hasHost)
                {
                    QJsonIO::SetValue(stream, query.queryItemValue("host"), { "wsSettings", "headers", "Host" });
                }
            }
            else if (type == "quic")
            {
                const auto hasQuicSecurity = query.hasQueryItem("quicSecurity");
                if (hasQuicSecurity)
                {
                    const auto quicSecurity = query.queryItemValue("quicSecurity");
                    QJsonIO::SetValue(stream, quicSecurity, { "quicSettings", "security" });

                    if (quicSecurity != "none")
                    {
                        const auto key = query.queryItemValue("key");
                        QJsonIO::SetValue(stream, key, { "quicSettings", "key" });
                    }
                }

                const auto hasHeaderType = query.hasQueryItem("headerType");
                const auto headerType = hasHeaderType ? query.queryItemValue("headerType") : "none";
                if (headerType != "none")
                    QJsonIO::SetValue(stream, headerType, { "quicSettings", "header", "type" });
            }
            else if (type == "grpc")
            {
                const auto hasServiceName = query.hasQueryItem("serviceName");
                if (hasServiceName)
                {
                    const auto serviceName = QUrl::fromPercentEncoding(query.queryItemValue("serviceName").toUtf8());
                    QJsonIO::SetValue(stream, serviceName, { "grpcSettings", "serviceName" });
                }

                const auto hasMode = query.hasQueryItem("mode");
                if (hasMode)
                {
                    const auto multiMode = QUrl::fromPercentEncoding(query.queryItemValue("mode").toUtf8()) == "multi";
                    QJsonIO::SetValue(stream, multiMode, { "grpcSettings", "multiMode" });
                }
            }
            else if (type == "xhttp")
            {
                for (const auto &key : { QStringLiteral("host"), QStringLiteral("path"), QStringLiteral("mode") })
                {
                    if (query.hasQueryItem(key))
                        QJsonIO::SetValue(stream, query.queryItemValue(key, QUrl::FullyDecoded), { "xhttpSettings", key });
                }

                if (query.hasQueryItem("extra"))
                {
                    QJsonParseError parseError;
                    const auto extraText = query.queryItemValue("extra", QUrl::FullyDecoded);
                    const auto extraDocument = QJsonDocument::fromJson(extraText.toUtf8(), &parseError);
                    if (parseError.error != QJsonParseError::NoError || !extraDocument.isObject())
                    {
                        *errMessage = QObject::tr("Invalid XHTTP extra JSON object");
                        return CONFIGROOT();
                    }
                    QJsonIO::SetValue(stream, extraDocument.object(), { "xhttpSettings", "extra" });
                }
            }

            // FinalMask is an opaque stream-level object. Decode it directly
            // from the original link so malformed percent escapes cannot be
            // normalized away by QUrl/QUrlQuery before validation.
            bool hasFinalMask = false;
            QString finalMaskText;
            if (!DecodeQueryItemStrict(str, QStringLiteral("fm"), &hasFinalMask, &finalMaskText))
            {
                *errMessage = QObject::tr("Invalid FinalMask query encoding");
                return CONFIGROOT();
            }
            if (hasFinalMask)
            {
                QJsonParseError parseError;
                const auto finalMaskDocument = QJsonDocument::fromJson(finalMaskText.toUtf8(), &parseError);
                if (parseError.error != QJsonParseError::NoError || !finalMaskDocument.isObject())
                {
                    *errMessage = QObject::tr("Invalid FinalMask JSON object");
                    return CONFIGROOT();
                }
                QJsonIO::SetValue(stream, finalMaskDocument.object(), "finalmask");
            }

            // tls-wise settings
            const auto hasSecurity = query.hasQueryItem("security");
            const auto security = hasSecurity ? query.queryItemValue("security") : "none";
            if (!QStringList{ "none", "tls", "reality" }.contains(security))
            {
                *errMessage = QObject::tr("Unsupported VLESS stream security: %1").arg(security);
                return CONFIGROOT();
            }
            const auto tlsKey = security == "reality" ? "realitySettings" : "tlsSettings";
            if (security != "none")
            {
                QJsonIO::SetValue(stream, security, "security");
            }
            // sni
            const auto hasSNI = query.hasQueryItem("sni");
            if (hasSNI)
            {
                const auto sni = query.queryItemValue("sni");
                QJsonIO::SetValue(stream, sni, { tlsKey, "serverName" });
            }
            // alpn
            const auto hasALPN = query.hasQueryItem("alpn");
            if (hasALPN && security != "reality")
            {
                const auto alpnRaw = QUrl::fromPercentEncoding(query.queryItemValue("alpn").toUtf8());
                const auto alpnArray = QJsonArray::fromStringList(alpnRaw.split(","));
                QJsonIO::SetValue(stream, alpnArray, { tlsKey, "alpn" });
            }
            // VLESS flow is independent of stream security (for example,
            // Vision is used with both TLS and REALITY).
            if (query.hasQueryItem("flow"))
            {
                const auto flow = query.queryItemValue("flow");
                if (!QStringList{ "xtls-rprx-vision", "xtls-rprx-vision-udp443" }.contains(flow))
                {
                    *errMessage = QObject::tr("Unsupported VLESS flow: %1").arg(flow);
                    return CONFIGROOT();
                }
                QJsonIO::SetValue(outbound, flow, { "settings", "vnext", 0, "users", 0, "flow" });
            }
            if (query.hasQueryItem("fp"))
                QJsonIO::SetValue(stream, query.queryItemValue("fp"), { tlsKey, "fingerprint" });
            if (security == "reality")
            {
                // Share links retain the ecosystem-standard `pbk` name while
                // current Xray outbound JSON uses `password`.
                QString password;
                for (const auto &key : { "pbk", "password", "publicKey" })
                    if (password.isEmpty() && query.hasQueryItem(key))
                        password = query.queryItemValue(key);
                if (!password.isEmpty())
                    QJsonIO::SetValue(stream, password, { "realitySettings", "password" });
                if (query.hasQueryItem("sid"))
                    QJsonIO::SetValue(stream, query.queryItemValue("sid"), { "realitySettings", "shortId" });
                if (query.hasQueryItem("pqv"))
                    QJsonIO::SetValue(stream, query.queryItemValue("pqv"), { "realitySettings", "mldsa65Verify" });
                if (query.hasQueryItem("spx"))
                {
                    const auto spiderX = QUrl::fromPercentEncoding(query.queryItemValue("spx", QUrl::FullyEncoded).toUtf8());
                    QJsonIO::SetValue(stream, spiderX, { "realitySettings", "spiderX" });
                }
            }

            // assembling config
            CONFIGROOT root;
            outbound["streamSettings"] = stream;
            root["outbounds"] = QJsonArray{ outbound };

            // return
            return root;
        }
    } // namespace serialization::vless
} // namespace Qv2ray::core::connection