#pragma once

#include <QSet>
#include <QString>
#include <QVector>
#include <optional>

namespace Qv2ray::platform::windows::loopback
{
    struct AppContainer
    {
        QString sid;
        QString name;
        QString containerName;
        QString packageFullName;
        QString groupKey;
    };

    struct Inventory
    {
        QVector<AppContainer> apps;
        unsigned long error = 0;
    };

    struct Exemptions
    {
        QSet<QString> sids;
        unsigned long error = 0;
    };

    Inventory ReadInventory();
    Exemptions ReadExemptions();
    unsigned long ChangeExemptions(const QSet<QString> &targets, bool enable, quintptr parentWindow);
    QString ErrorMessage(unsigned long error);

    // Called before the normal application starts. The elevated instance only applies one request.
    std::optional<int> RunElevatedRequest(int argc, char *argv[]);
}
