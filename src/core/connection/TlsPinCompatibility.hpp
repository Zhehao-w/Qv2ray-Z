#pragma once

#include "base/Qv2rayBase.hpp"

namespace Qv2ray::core::connection::tls_pin
{
    inline QString NormalizeCurrentTlsPinForEditor(QString pin)
    {
        // Xray v26.3.27 accepts OpenSSL-style SHA-256 fingerprints by
        // removing ':' before hex decoding. The retained chain editor only
        // accepts 64 hexadecimal characters, so mirror Xray's normalization
        // at the editor boundary. Exact persisted formatting is still kept on
        // no-op save by comparing this normalized representation below.
        pin = pin.trimmed();
        pin.remove(':');
        return pin;
    }

    inline QStringList ParseCurrentTlsPin(const QString &value)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        auto pins = value.split(',', Qt::SkipEmptyParts);
#else
        auto pins = value.split(',', QString::SkipEmptyParts);
#endif
        for (auto &pin : pins)
            pin = NormalizeCurrentTlsPinForEditor(pin);
        pins.removeAll(QString{});
        return pins;
    }

    inline QJsonValue RuntimeJsonField(const QJsonObject &object, const QString &field)
    {
        // Go's JSON decoder accepts case-insensitive field names. Ambiguous
        // case variants are rejected below before interpreting these fields.
        QJsonValue value(QJsonValue::Undefined);
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            if (it.key().compare(field, Qt::CaseInsensitive) == 0)
                value = it.value();
        return value;
    }

    inline bool HasEffectiveTlsVerificationName(const QJsonObject &tls)
    {
        const auto serverName = RuntimeJsonField(tls, "serverName").toString();
        // Xray maps the case-insensitive fromMitm sentinel to an empty name.
        // It must not satisfy the guard on transports that do not infer one.
        if (!serverName.trimmed().isEmpty() && serverName.compare(QStringLiteral("fromMitm"), Qt::CaseInsensitive) != 0)
            return true;

        // Xray parses this supported alternate verifier as a comma-separated
        // list of trimmed names, skipping empty entries. Preserve the raw text.
        for (const auto &name : RuntimeJsonField(tls, "verifyPeerCertByName").toString().split(','))
            if (!name.trimmed().isEmpty())
                return true;
        return false;
    }

    inline std::optional<QString> ValidateRuntimeJsonFieldNames(const QJsonObject &object, const QStringList &fields, const QString &location)
    {
        for (const auto &field : fields)
        {
            int matches = 0;
            for (auto it = object.constBegin(); it != object.constEnd(); ++it)
                if (it.key().compare(field, Qt::CaseInsensitive) == 0)
                    ++matches;
            if (matches > 1)
                return QObject::tr("Cannot start connection: %1 contains multiple case variants of %2. "
                                   "Use a single field spelling so the TLS pin safety check can verify the effective Xray settings.")
                    .arg(location, field);
        }
        return std::nullopt;
    }

    inline std::optional<QString> ValidateRuntimeTlsPins(const QJsonObject &root)
    {
        // Inspect the final raw JSON, not the editor's typed model: imported,
        // complex and expanded outbounds can bypass the editor entirely.
        if (const auto error = ValidateRuntimeJsonFieldNames(root, { "outbounds" }, QStringLiteral("configuration")); error)
            return error;
        const auto outbounds = RuntimeJsonField(root, "outbounds").toArray();
        for (int index = 0; index < outbounds.size(); ++index)
        {
            const auto outbound = outbounds.at(index).toObject();
            auto location = QStringLiteral("outbounds[%1]").arg(index);
            const auto tag = RuntimeJsonField(outbound, "tag").toString();
            if (!tag.isEmpty())
                location += QStringLiteral(" (%1)").arg(tag);

            if (const auto error = ValidateRuntimeJsonFieldNames(outbound, { "streamSettings" }, location); error)
                return error;

            QList<QPair<QJsonObject, QString>> streams{ { RuntimeJsonField(outbound, "streamSettings").toObject(), location + ".streamSettings" } };
            while (!streams.isEmpty())
            {
                const auto entry = streams.takeLast();
                const auto &stream = entry.first;
                if (const auto error = ValidateRuntimeJsonFieldNames(
                        stream, { "security", "tlsSettings", "network", "xhttpSettings", "splithttpSettings" }, entry.second);
                    error)
                    return error;
                const auto tls = RuntimeJsonField(stream, "tlsSettings").toObject();
                const bool usesTls = RuntimeJsonField(stream, "security").toString().compare(QStringLiteral("tls"), Qt::CaseInsensitive) == 0;
                if (usesTls)
                    if (const auto error = ValidateRuntimeJsonFieldNames(tls, { "pinnedPeerCertSha256", "serverName", "verifyPeerCertByName" },
                                                                         entry.second + ".tlsSettings");
                        error)
                        return error;
                const auto pin = RuntimeJsonField(tls, "pinnedPeerCertSha256");
                if (usesTls && pin.isString() && !ParseCurrentTlsPin(pin.toString()).isEmpty() && !HasEffectiveTlsVerificationName(tls))
                {
                    return QObject::tr("Cannot start connection: %1 uses pinnedPeerCertSha256 without an effective certificate verification name. "
                                       "The bundled Xray v26.3.27 is affected by GHSA-5wf9-h793-w73c. "
                                       "Set the intended name in tlsSettings.serverName (not fromMitm), or supply a non-empty name list in "
                                       "tlsSettings.verifyPeerCertByName.")
                        .arg(entry.second);
                }

                // XHTTP can use a separate TLS download stream. Xray's extra
                // object replaces transport settings (except host/path/mode),
                // so inspect only the effective downloadSettings. Do not walk
                // arbitrary opaque objects that merely resemble TLS settings.
                const auto network = RuntimeJsonField(stream, "network").toString().toLower();
                if (network != QStringLiteral("xhttp") && network != QStringLiteral("splithttp"))
                    continue;

                const auto field =
                    RuntimeJsonField(stream, "xhttpSettings").isObject() ? QStringLiteral("xhttpSettings") : QStringLiteral("splithttpSettings");
                auto transport = RuntimeJsonField(stream, field).toObject();
                auto downloadLocation = entry.second + "." + field;
                if (const auto error = ValidateRuntimeJsonFieldNames(transport, { "extra" }, downloadLocation); error)
                    return error;
                if (!RuntimeJsonField(transport, "extra").isUndefined())
                {
                    transport = RuntimeJsonField(transport, "extra").toObject();
                    downloadLocation += ".extra";
                }
                if (const auto error = ValidateRuntimeJsonFieldNames(transport, { "downloadSettings" }, downloadLocation); error)
                    return error;
                if (RuntimeJsonField(transport, "downloadSettings").isObject())
                    streams.append({ RuntimeJsonField(transport, "downloadSettings").toObject(), downloadLocation + ".downloadSettings" });
            }
        }
        return std::nullopt;
    }

    inline void PrepareTlsPinEditorModel(const QJsonObject &originalStream, StreamSettingsObject &stream)
    {
        // The legacy typed member is retained only as the existing editor's
        // internal list storage. Never surface the removed Xray field as an
        // editable current pin, otherwise merely opening and saving an old
        // config would migrate it implicitly.
        stream.tlsSettings.pinnedPeerCertificateChainSha256.clear();

        const auto tls = originalStream.value("tlsSettings").toObject();
        const auto currentPin = tls.value("pinnedPeerCertSha256");
        if (currentPin.isString())
            stream.tlsSettings.pinnedPeerCertificateChainSha256 = ParseCurrentTlsPin(currentPin.toString());
    }

    inline QStringList EditorPinsFromTlsJson(const QJsonObject &tls)
    {
        QStringList pins;
        const auto editorValue = tls.value("pinnedPeerCertificateChainSha256");
        if (!editorValue.isArray())
            return pins;

        for (const auto &entry : editorValue.toArray())
        {
            const auto pin = entry.toString().trimmed();
            if (!pin.isEmpty())
                pins.push_back(pin);
        }
        return pins;
    }

    inline void FinalizeTlsPinForXray(const QJsonObject &originalStream, QJsonObject &editedStream)
    {
        const bool hadEditedTlsSettings = editedStream.contains("tlsSettings");
        auto tls = editedStream.value("tlsSettings").toObject();
        const auto editedPins = EditorPinsFromTlsJson(tls);

        // Never serialize the removed Xray key from the legacy typed model.
        tls.remove("pinnedPeerCertificateChainSha256");

        if (editedStream.value("security").toString() != QStringLiteral("tls"))
        {
            tls.remove("pinnedPeerCertSha256");
            if (hadEditedTlsSettings && !tls.isEmpty())
                editedStream.insert("tlsSettings", tls);
            else
                editedStream.remove("tlsSettings");
            return;
        }

        // Convert the editor representation without discarding an unnamed pin.
        // The editor and startup validators check the final preserved JSON,
        // which may contain an opaque verifyPeerCertByName alternate verifier.
        const auto originalTls = originalStream.value("tlsSettings").toObject();
        const auto originalCurrent = originalTls.value("pinnedPeerCertSha256");

        if (editedPins.isEmpty())
        {
            // Preserve an unrepresentable current value rather than silently
            // deleting it on an unrelated GUI save. For strings, an empty
            // parsed list is a no-op only when the persisted value was already
            // empty/whitespace; otherwise an empty editor list means the user
            // explicitly cleared a previously representable value.
            if (!originalCurrent.isUndefined() && !originalCurrent.isString())
                tls.insert("pinnedPeerCertSha256", originalCurrent);
            else if (originalCurrent.isString() && ParseCurrentTlsPin(originalCurrent.toString()).isEmpty())
                tls.insert("pinnedPeerCertSha256", originalCurrent);
            else
                tls.remove("pinnedPeerCertSha256");
        }
        else
        {
            // Avoid no-op formatting churn. Xray accepts comma-separated hex,
            // optional OpenSSL ':' separators, and surrounding whitespace, so
            // retain the exact persisted text when the editor-visible pin list
            // did not change.
            if (originalCurrent.isString() && ParseCurrentTlsPin(originalCurrent.toString()) == editedPins)
                tls.insert("pinnedPeerCertSha256", originalCurrent);
            else
                tls.insert("pinnedPeerCertSha256", editedPins.join(','));
        }

        editedStream.insert("tlsSettings", tls);
    }
} // namespace Qv2ray::core::connection::tls_pin
