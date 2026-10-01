#include "StyleManager.hpp"

#include "base/Qv2rayBase.hpp"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

#define QV_MODULE_NAME "StyleManager"

namespace Qv2ray::ui::styles
{
    QvStyleManager::QvStyleManager(QObject *parent) : QObject(parent)
    {
    }

    void QvStyleManager::ApplyStyle()
    {
        // Keep the rendering base deterministic across supported Windows
        // systems, then layer the maintained first-party visual system on top.
        qApp->setStyle(QStyleFactory::create("Fusion"));

        QPalette palette;
        palette.setColor(QPalette::Window, QColor(247, 247, 247));
        palette.setColor(QPalette::WindowText, QColor(31, 31, 31));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(250, 250, 250));
        palette.setColor(QPalette::ToolTipBase, Qt::white);
        palette.setColor(QPalette::ToolTipText, QColor(31, 31, 31));
        palette.setColor(QPalette::Text, QColor(31, 31, 31));
        palette.setColor(QPalette::Button, QColor(251, 251, 251));
        palette.setColor(QPalette::ButtonText, QColor(31, 31, 31));
        palette.setColor(QPalette::BrightText, Qt::red);
        palette.setColor(QPalette::Link, QColor(0, 95, 184));
        palette.setColor(QPalette::Highlight, QColor(0, 120, 212));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::Disabled, QPalette::Text, QColor(145, 145, 145));
        palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(145, 145, 145));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(145, 145, 145));
        qApp->setPalette(palette);

        QFile stylesheet(":/assets/styles/qv2ray-modern.qss");
        if (!stylesheet.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            LOG("Cannot open the built-in Qv2ray-Z stylesheet.");
            qApp->setStyleSheet({});
            return;
        }

        qApp->setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    }
} // namespace Qv2ray::ui::styles
