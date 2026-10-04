#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Qv2ray::base::single_server_settings
{
    inline bool ManagedFieldChanged(const QJsonObject &baseline, const QJsonObject &edited, const QString &key)
    {
        return baseline.contains(key) != edited.contains(key) || baseline.value(key) != edited.value(key);
    }

    inline bool HasManagedChanges(const QJsonObject &baseline, const QJsonObject &edited, const QStringList &managedFields)
    {
        for (const auto &key : managedFields)
            if (ManagedFieldChanged(baseline, edited, key))
                return true;
        return false;
    }

    inline void ApplyManagedChanges(QJsonObject &target, const QJsonObject &baseline, const QJsonObject &edited,
                                    const QStringList &managedFields)
    {
        for (const auto &key : managedFields)
        {
            if (!ManagedFieldChanged(baseline, edited, key))
                continue;
            if (edited.contains(key))
                target.insert(key, edited.value(key));
            else
                target.remove(key);
        }
    }

    inline QJsonObject FirstObject(const QJsonArray &array)
    {
        return !array.isEmpty() && array.first().isObject() ? array.first().toObject() : QJsonObject{};
    }

    inline bool ManagedStringFieldsEmpty(const QJsonObject &object, const QStringList &managedFields)
    {
        for (const auto &key : managedFields)
            if (!object.value(key).toString().isEmpty())
                return false;
        return true;
    }

    // Graphical protocol editors in Qv2ray-Z intentionally expose only one
    // server and, for VMess/HTTP/SOCKS, only the first user. Patch the fields
    // those editors actually manage instead of serializing the typed model over
    // the complete settings object. This keeps unknown modern Xray fields and
    // additional server/user entries byte-for-JSON-value compatible.
    inline QJsonObject ApplyManagedFirstServerChanges(const QJsonObject &originalSettings, const QString &serverArrayField,
                                                      const QJsonObject &baselineServer, const QJsonObject &editedServer,
                                                      const QStringList &managedServerFields, const QString &userArrayField = {},
                                                      const QStringList &managedUserFields = {}, bool removeEmptyFirstUser = false)
    {
        const auto baselineUsers = baselineServer.value(userArrayField).toArray();
        const auto editedUsers = editedServer.value(userArrayField).toArray();
        const auto baselineUser = FirstObject(baselineUsers);
        const auto editedUser = FirstObject(editedUsers);
        const auto serverChanged = HasManagedChanges(baselineServer, editedServer, managedServerFields);
        const auto userChanged = !managedUserFields.isEmpty() && HasManagedChanges(baselineUser, editedUser, managedUserFields);

        if (!serverChanged && !userChanged)
            return originalSettings;

        // No persisted server exists yet: there is no opaque state to retain,
        // so use the editor model as the initial representation.
        if (!originalSettings.contains(serverArrayField))
        {
            auto result = originalSettings;
            result.insert(serverArrayField, QJsonArray{ editedServer });
            return result;
        }

        if (!originalSettings.value(serverArrayField).isArray())
            return originalSettings;

        auto servers = originalSettings.value(serverArrayField).toArray();
        if (servers.isEmpty())
        {
            auto result = originalSettings;
            result.insert(serverArrayField, QJsonArray{ editedServer });
            return result;
        }
        if (!servers.first().isObject())
            return originalSettings;

        auto server = servers.first().toObject();
        ApplyManagedChanges(server, baselineServer, editedServer, managedServerFields);

        if (userChanged)
        {
            if (server.contains(userArrayField) && !server.value(userArrayField).isArray())
                return originalSettings;

            auto users = server.value(userArrayField).toArray();
            if (removeEmptyFirstUser && ManagedStringFieldsEmpty(editedUser, managedUserFields))
            {
                if (!users.isEmpty())
                    users.removeFirst();
                if (users.isEmpty())
                    server.remove(userArrayField);
                else
                    server.insert(userArrayField, users);
            }
            else if (users.isEmpty())
            {
                users.append(editedUser);
                server.insert(userArrayField, users);
            }
            else
            {
                if (!users.first().isObject())
                    return originalSettings;
                auto user = users.first().toObject();
                ApplyManagedChanges(user, baselineUser, editedUser, managedUserFields);
                users[0] = user;
                server.insert(userArrayField, users);
            }
        }

        servers[0] = server;
        auto result = originalSettings;
        result.insert(serverArrayField, servers);
        return result;
    }
} // namespace Qv2ray::base::single_server_settings
