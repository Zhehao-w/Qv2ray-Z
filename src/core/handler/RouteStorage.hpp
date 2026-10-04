#pragma once

#include "utils/QvHelpers.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace Qv2ray::core::handler::route_storage
{
    enum class RouteStorageState
    {
        Missing,
        Valid,
        Invalid,
        Unreadable,
    };

    struct RouteStorageSnapshot
    {
        RouteStorageState state = RouteStorageState::Missing;
        QJsonObject object;
        QByteArray rawBytes;
        QString error;
    };

    inline bool IsRouteStorageWritable(RouteStorageState state)
    {
        return state == RouteStorageState::Missing || state == RouteStorageState::Valid;
    }

    inline RouteStorageSnapshot LoadRouteStorage(const QString &path)
    {
        QFileInfo info(path);
        if (!info.exists())
        {
            if (info.isSymLink())
                return { RouteStorageState::Unreadable, {}, {}, QStringLiteral("routes.json is an unresolved symbolic link") };
            return { RouteStorageState::Missing, {}, {}, {} };
        }

        if (!info.isFile())
            return { RouteStorageState::Unreadable, {}, {}, QStringLiteral("routes.json is not a regular file") };

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return { RouteStorageState::Unreadable, {}, {}, file.errorString() };

        const auto rawBytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            return { RouteStorageState::Unreadable, {}, rawBytes, file.errorString() };

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(rawBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError)
            return { RouteStorageState::Invalid, {}, rawBytes, parseError.errorString() };

        if (!document.isObject())
            return { RouteStorageState::Invalid, {}, rawBytes, QStringLiteral("routes.json root is not an object") };

        return { RouteStorageState::Valid, document.object(), rawBytes, {} };
    }

    inline bool SaveRouteStorage(const QString &path, RouteStorageState state, const QJsonObject &object, QString *error = nullptr)
    {
        if (!IsRouteStorageWritable(state))
        {
            if (error)
                *error = QStringLiteral("routes.json is invalid or unreadable; ordinary save is blocked until explicit recovery");
            return false;
        }

        if (!StringToFile(JsonToString(object), path))
        {
            if (error)
                *error = QStringLiteral("failed to write routes.json");
            return false;
        }

        if (error)
            error->clear();
        return true;
    }
} // namespace Qv2ray::core::handler::route_storage
