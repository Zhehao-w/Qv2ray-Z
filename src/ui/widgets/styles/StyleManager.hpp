#pragma once

#include <QObject>

namespace Qv2ray::ui::styles
{
    // Qv2ray-Z intentionally exposes one maintained first-party interface.
    // This object remains the central application-style installer, but it no
    // longer discovers Qt factory styles or user-supplied theme files.
    class QvStyleManager : public QObject
    {
      public:
        explicit QvStyleManager(QObject *parent = nullptr);
        void ApplyStyle();

      protected:
        bool eventFilter(QObject *watched, QEvent *event) override;
    };

    inline QvStyleManager *StyleManager = nullptr;
} // namespace Qv2ray::ui::styles

using namespace Qv2ray::ui::styles;
