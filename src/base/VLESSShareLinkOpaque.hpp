#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

namespace Qv2ray::base::vless_share
{
    inline const QString &OpaqueQueryMetadataKey()
    {
        static const auto key = QStringLiteral("_QV2RAY_VLESS_OPAQUE_QUERY_");
        return key;
    }

    inline const QSet<QString> &ManagedQueryKeys()
    {
        // Keep this aligned with the VLESS parser/serializer. These fields are
        // modeled by Qv2ray-Z and must remain editor-authoritative rather than
        // being restored from opaque import metadata after a user changes them.
        static const QSet<QString> keys{
            QStringLiteral("type"),         QStringLiteral("encryption"), QStringLiteral("seed"),       QStringLiteral("headerType"),
            QStringLiteral("path"),         QStringLiteral("host"),       QStringLiteral("quicSecurity"), QStringLiteral("key"),
            QStringLiteral("serviceName"),  QStringLiteral("mode"),       QStringLiteral("extra"),      QStringLiteral("fm"),
            QStringLiteral("security"),     QStringLiteral("sni"),        QStringLiteral("alpn"),       QStringLiteral("flow"),
            QStringLiteral("fp"),           QStringLiteral("pbk"),        QStringLiteral("password"),   QStringLiteral("publicKey"),
            QStringLiteral("sid"),          QStringLiteral("pqv"),        QStringLiteral("spx")
        };
        return keys;
    }

    inline QJsonArray ExtractOpaqueQueryItems(const QString &link)
    {
        const QUrl url(link);
        if (!url.isValid() || url.scheme() != QStringLiteral("vless"))
            return {};

        const QUrlQuery query(url);
        QJsonArray result;
        for (const auto &item : query.queryItems(QUrl::FullyDecoded))
        {
            if (ManagedQueryKeys().contains(item.first))
                continue;
            result.append(QJsonObject{ { QStringLiteral("key"), item.first }, { QStringLiteral("value"), item.second } });
        }
        return result;
    }

    inline QString AppendOpaqueQueryItems(const QString &serializedLink, const QJsonArray &opaqueItems)
    {
        if (opaqueItems.isEmpty() || !serializedLink.startsWith(QStringLiteral("vless://")))
            return serializedLink;

        QUrl url(serializedLink);
        if (!url.isValid())
            return QStringLiteral("(Invalid VLESS opaque query metadata)");

        QUrlQuery query(url);
        QSet<QString> serializerOwnedKeys;
        for (const auto &item : query.queryItems(QUrl::FullyDecoded))
            serializerOwnedKeys.insert(item.first);

        for (const auto &value : opaqueItems)
        {
            if (!value.isObject())
                return QStringLiteral("(Invalid VLESS opaque query metadata)");
            const auto object = value.toObject();
            if (!object.value(QStringLiteral("key")).isString() || !object.value(QStringLiteral("value")).isString())
                return QStringLiteral("(Invalid VLESS opaque query metadata)");

            const auto key = object.value(QStringLiteral("key")).toString();
            const auto itemValue = object.value(QStringLiteral("value")).toString();
            if (ManagedQueryKeys().contains(key) || serializerOwnedKeys.contains(key))
                continue;
            query.addQueryItem(key, itemValue);
        }

        url.setQuery(query);
        return url.toString(QUrl::FullyEncoded);
    }
} // namespace Qv2ray::base::vless_share
