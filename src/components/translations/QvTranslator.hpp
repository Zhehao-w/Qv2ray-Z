#pragma once

#include <QString>
#include <QStringList>
#include <memory>

namespace Qv2ray::common
{
    // Source-compatibility shim for the legacy Preferences implementation.
    // Qv2ray-Z is English-only: no QTranslator is created and no external or
    // embedded translation resources are discovered or loaded.
    class QvTranslator
    {
      public:
        explicit QvTranslator() = default;

        QStringList GetAvailableLanguages() const
        {
            return { QStringLiteral("en_US") };
        }

        bool InstallTranslation(const QString &code) const
        {
            return code == QStringLiteral("en_US");
        }
    };

    inline std::unique_ptr<common::QvTranslator> Qv2rayTranslator;
} // namespace Qv2ray::common

using namespace Qv2ray::common;
