#pragma once

#include "base/Qv2rayBase.hpp"

namespace Qv2ray::core::connection::tls_pin
{
    inline QStringList ParseCurrentTlsPin(const QString &value)
    {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        auto pins = value.split(',', Qt::SkipEmptyParts);
#else
        auto pins = value.split(',', QString::SkipEmptyParts);
#endif
        for (auto &pin : pins)
            pin = pin.trimmed();
        pins.removeAll(QString{});
        return pins;
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
            // Avoid no-op formatting churn. Xray accepts comma-separated hex
            // with surrounding whitespace, so retain the exact persisted text
            // when the editor-visible pin list did not change.
            if (originalCurrent.isString() && ParseCurrentTlsPin(originalCurrent.toString()) == editedPins)
                tls.insert("pinnedPeerCertSha256", originalCurrent);
            else
                tls.insert("pinnedPeerCertSha256", editedPins.join(','));
        }

        editedStream.insert("tlsSettings", tls);
    }
} // namespace Qv2ray::core::connection::tls_pin
