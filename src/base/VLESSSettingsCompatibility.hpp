#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace Qv2ray::base::vless_settings
{
    enum class Representation
    {
        Flat,
        VNext,
        Unsupported
    };

    inline Representation DetectRepresentation(const QJsonObject &settings)
    {
        // Xray's simplified VLESS outbound form is authoritative whenever a
        // direct address is present. Keep that precedence even if a stale or
        // opaque vnext value is also present.
        if (settings.contains(QStringLiteral("address")))
            return settings.value(QStringLiteral("address")).isString() ? Representation::Flat : Representation::Unsupported;

        // New editor content historically starts in vnext form.
        if (settings.isEmpty())
            return Representation::VNext;

        if (!settings.value(QStringLiteral("vnext")).isArray())
            return Representation::Unsupported;

        const auto vnext = settings.value(QStringLiteral("vnext")).toArray();
        if (vnext.isEmpty() || !vnext.first().isObject())
            return Representation::Unsupported;

        const auto server = vnext.first().toObject();
        if (!server.value(QStringLiteral("users")).isArray())
            return Representation::Unsupported;

        const auto users = server.value(QStringLiteral("users")).toArray();
        if (users.isEmpty() || !users.first().isObject())
            return Representation::Unsupported;

        return Representation::VNext;
    }

    inline QJsonObject FirstUser(const QJsonObject &server)
    {
        const auto users = server.value(QStringLiteral("users")).toArray();
        return !users.isEmpty() && users.first().isObject() ? users.first().toObject() : QJsonObject{};
    }

    inline QJsonObject ServerForEditing(const QJsonObject &settings)
    {
        switch (DetectRepresentation(settings))
        {
            case Representation::VNext:
            {
                const auto vnext = settings.value(QStringLiteral("vnext")).toArray();
                return !vnext.isEmpty() && vnext.first().isObject() ? vnext.first().toObject() : QJsonObject{};
            }
            case Representation::Flat:
            {
                QJsonObject server;
                for (const auto &key : { QStringLiteral("address"), QStringLiteral("port") })
                {
                    if (settings.contains(key))
                        server.insert(key, settings.value(key));
                }

                QJsonObject user;
                for (const auto &key : { QStringLiteral("id"), QStringLiteral("encryption"), QStringLiteral("flow") })
                {
                    if (settings.contains(key))
                        user.insert(key, settings.value(key));
                }
                if (!user.isEmpty())
                    server.insert(QStringLiteral("users"), QJsonArray{ user });
                return server;
            }
            case Representation::Unsupported: return {};
        }
        return {};
    }

    inline void ApplyChangedField(QJsonObject &target, const QJsonObject &baseline, const QJsonObject &edited, const QString &key)
    {
        if (baseline.value(key) == edited.value(key))
            return;
        if (edited.contains(key))
            target.insert(key, edited.value(key));
        else
            target.remove(key);
    }

    inline bool HasManagedChanges(const QJsonObject &baselineServer, const QJsonObject &editedServer)
    {
        for (const auto &key : { QStringLiteral("address"), QStringLiteral("port") })
        {
            if (baselineServer.value(key) != editedServer.value(key))
                return true;
        }

        const auto baselineUser = FirstUser(baselineServer);
        const auto editedUser = FirstUser(editedServer);
        for (const auto &key : { QStringLiteral("id"), QStringLiteral("encryption"), QStringLiteral("flow") })
        {
            if (baselineUser.value(key) != editedUser.value(key))
                return true;
        }
        return false;
    }

    inline QJsonObject ApplyManagedServerChanges(const QJsonObject &originalSettings, const QJsonObject &baselineServer,
                                                 const QJsonObject &editedServer)
    {
        const auto representation = DetectRepresentation(originalSettings);
        if (representation == Representation::Unsupported || !HasManagedChanges(baselineServer, editedServer))
            return originalSettings;

        auto result = originalSettings;
        if (representation == Representation::Flat)
        {
            ApplyChangedField(result, baselineServer, editedServer, QStringLiteral("address"));
            ApplyChangedField(result, baselineServer, editedServer, QStringLiteral("port"));

            const auto baselineUser = FirstUser(baselineServer);
            const auto editedUser = FirstUser(editedServer);
            for (const auto &key : { QStringLiteral("id"), QStringLiteral("encryption"), QStringLiteral("flow") })
                ApplyChangedField(result, baselineUser, editedUser, key);
            return result;
        }

        auto vnext = result.value(QStringLiteral("vnext")).toArray();
        if (vnext.isEmpty())
            vnext.append(QJsonObject{});
        auto server = vnext.first().toObject();
        ApplyChangedField(server, baselineServer, editedServer, QStringLiteral("address"));
        ApplyChangedField(server, baselineServer, editedServer, QStringLiteral("port"));

        const auto baselineUser = FirstUser(baselineServer);
        const auto editedUser = FirstUser(editedServer);
        auto users = server.value(QStringLiteral("users")).toArray();
        if (users.isEmpty())
            users.append(QJsonObject{});
        auto user = users.first().toObject();
        for (const auto &key : { QStringLiteral("id"), QStringLiteral("encryption"), QStringLiteral("flow") })
            ApplyChangedField(user, baselineUser, editedUser, key);
        users[0] = user;
        server.insert(QStringLiteral("users"), users);
        vnext[0] = server;
        result.insert(QStringLiteral("vnext"), vnext);
        return result;
    }

    inline QJsonObject SetHostAddress(const QJsonObject &settings, const QString &address, const int port)
    {
        const auto baseline = ServerForEditing(settings);
        if (DetectRepresentation(settings) == Representation::Unsupported)
            return settings;

        auto edited = baseline;
        edited.insert(QStringLiteral("address"), address);
        edited.insert(QStringLiteral("port"), port);
        return ApplyManagedServerChanges(settings, baseline, edited);
    }
} // namespace Qv2ray::base::vless_settings
