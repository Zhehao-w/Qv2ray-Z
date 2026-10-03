#pragma once

#include <QByteArray>
#include <QJsonArray>
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
            QStringLiteral("type"),         QStringLiteral("encryption"), QStringLiteral("seed"),         QStringLiteral("headerType"),
            QStringLiteral("path"),         QStringLiteral("host"),       QStringLiteral("quicSecurity"), QStringLiteral("key"),
            QStringLiteral("serviceName"),  QStringLiteral("mode"),       QStringLiteral("extra"),        QStringLiteral("fm"),
            QStringLiteral("security"),     QStringLiteral("sni"),        QStringLiteral("alpn"),         QStringLiteral("flow"),
            QStringLiteral("fp"),           QStringLiteral("pbk"),        QStringLiteral("password"),     QStringLiteral("publicKey"),
            QStringLiteral("sid"),          QStringLiteral("pqv"),        QStringLiteral("spx")
        };
        return keys;
    }

    inline bool HasValidPercentEscapes(const QString &encoded)
    {
        const auto isHex = [](const QChar c)
        {
            const auto value = c.unicode();
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
        };

        for (int i = 0; i < encoded.size(); ++i)
        {
            if (encoded.at(i) != '%')
                continue;
            if (i + 2 >= encoded.size() || !isHex(encoded.at(i + 1)) || !isHex(encoded.at(i + 2)))
                return false;
            i += 2;
        }
        return true;
    }

    inline QString DecodeQueryKey(const QString &encoded)
    {
        return QString::fromUtf8(QByteArray::fromPercentEncoding(encoded.toUtf8()));
    }

    inline QString RawQueryPart(const QString &link)
    {
        const auto queryStart = link.indexOf('?');
        if (queryStart < 0)
            return {};
        const auto fragmentStart = link.indexOf('#', queryStart + 1);
        return fragmentStart < 0 ? link.mid(queryStart + 1) : link.mid(queryStart + 1, fragmentStart - queryStart - 1);
    }

    inline QJsonArray ExtractOpaqueQueryItems(const QString &link)
    {
        QJsonArray result;
        const auto rawQuery = RawQueryPart(link);
        if (rawQuery.isEmpty())
            return result;

        for (const auto &rawItem : rawQuery.split('&', Qt::KeepEmptyParts))
        {
            if (rawItem.isEmpty())
                continue;
            const auto equals = rawItem.indexOf('=');
            const auto rawKey = equals < 0 ? rawItem : rawItem.left(equals);
            if (!HasValidPercentEscapes(rawKey))
            {
                result.append(rawItem);
                continue;
            }
            if (!ManagedQueryKeys().contains(DecodeQueryKey(rawKey)))
                result.append(rawItem);
        }
        return result;
    }

    inline QString AppendOpaqueQueryItems(const QString &serializedLink, const QJsonArray &opaqueItems)
    {
        if (opaqueItems.isEmpty() || !serializedLink.startsWith(QStringLiteral("vless://")))
            return serializedLink;

        QSet<QString> serializerOwnedKeys;
        const auto serializedRawQuery = RawQueryPart(serializedLink);
        for (const auto &rawItem : serializedRawQuery.split('&', Qt::SkipEmptyParts))
        {
            const auto equals = rawItem.indexOf('=');
            const auto rawKey = equals < 0 ? rawItem : rawItem.left(equals);
            if (HasValidPercentEscapes(rawKey))
                serializerOwnedKeys.insert(DecodeQueryKey(rawKey));
        }

        QStringList validatedItems;
        for (const auto &value : opaqueItems)
        {
            if (!value.isString())
                return QStringLiteral("(Invalid VLESS opaque query metadata)");
            const auto rawItem = value.toString();
            if (rawItem.isEmpty() || rawItem.contains('&') || rawItem.contains('#'))
                return QStringLiteral("(Invalid VLESS opaque query metadata)");

            const auto equals = rawItem.indexOf('=');
            const auto rawKey = equals < 0 ? rawItem : rawItem.left(equals);
            const auto rawValue = equals < 0 ? QString{} : rawItem.mid(equals + 1);
            if (rawKey.isEmpty() || rawKey.contains('=') || !HasValidPercentEscapes(rawKey) || !HasValidPercentEscapes(rawValue))
                return QStringLiteral("(Invalid VLESS opaque query metadata)");

            const auto key = DecodeQueryKey(rawKey);
            if (ManagedQueryKeys().contains(key) || serializerOwnedKeys.contains(key))
                continue;
            validatedItems << rawItem;
        }

        if (validatedItems.isEmpty())
            return serializedLink;

        const auto fragmentStart = serializedLink.indexOf('#');
        const auto beforeFragment = fragmentStart < 0 ? serializedLink : serializedLink.left(fragmentStart);
        const auto fragment = fragmentStart < 0 ? QString{} : serializedLink.mid(fragmentStart);
        const auto separator = beforeFragment.contains('?') ? (beforeFragment.endsWith('?') ? QString{} : QStringLiteral("&")) : QStringLiteral("?");
        return beforeFragment + separator + validatedItems.join('&') + fragment;
    }
} // namespace Qv2ray::base::vless_share
