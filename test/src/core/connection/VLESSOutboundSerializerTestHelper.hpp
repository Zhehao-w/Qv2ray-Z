#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>

// Keeps the built-in protocol plugin's historical object model out of tests
// which also include the core connection object model.
QString SerializeVLESSOutboundForTest(const QString &alias, const QJsonObject &settings, const QJsonObject &streamSettings);
QPair<QString, int> GetVLESSOutboundHostForTest(const QJsonObject &settings);
QJsonObject SetVLESSOutboundHostForTest(const QJsonObject &settings, const QString &address, int port);
