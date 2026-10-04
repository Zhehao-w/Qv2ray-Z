#include "base/SingleServerSettingsCompatibility.hpp"
#include "core/connection/OutboundEditorPersistence.hpp"

#include "catch.hpp"

using Qv2ray::base::single_server_settings::ApplyManagedFirstServerChanges;
using Qv2ray::core::connection::ReplaceEditedSingleOutbound;

TEST_CASE("Graphical single-outbound edits preserve the stored connection root")
{
    OUTBOUND originalOutbound;
    originalOutbound.insert(QStringLiteral("protocol"), QStringLiteral("vmess"));
    originalOutbound.insert(QStringLiteral("tag"), QStringLiteral("old"));

    QJsonArray outbounds;
    outbounds.append(originalOutbound);

    CONFIGROOT original;
    original.insert(QStringLiteral("outbounds"), outbounds);
    original.insert(QStringLiteral("policy"), QJsonObject{ { QStringLiteral("opaquePolicy"), 7 } });
    original.insert(QStringLiteral("dns"), QJsonObject{ { QStringLiteral("opaqueDns"), true } });
    original.insert(QStringLiteral("futureRoot"), QJsonObject{ { QStringLiteral("nested"), QJsonArray{ 1, 2, 3 } } });

    OUTBOUND editedOutbound = originalOutbound;
    editedOutbound.insert(QStringLiteral("tag"), QStringLiteral("edited"));

    const auto result = ReplaceEditedSingleOutbound(original, editedOutbound);
    REQUIRE(result.value(QStringLiteral("policy")) == original.value(QStringLiteral("policy")));
    REQUIRE(result.value(QStringLiteral("dns")) == original.value(QStringLiteral("dns")));
    REQUIRE(result.value(QStringLiteral("futureRoot")) == original.value(QStringLiteral("futureRoot")));

    const auto resultOutbounds = result.value(QStringLiteral("outbounds")).toArray();
    REQUIRE(resultOutbounds.size() == 1);
    REQUIRE(resultOutbounds.at(0).toObject().value(QStringLiteral("tag")) == QStringLiteral("edited"));
}

TEST_CASE("No-op single-server persistence returns opaque settings unchanged")
{
    const QJsonObject opaqueUser{ { QStringLiteral("id"), QStringLiteral("id-1") },
                                  { QStringLiteral("alterId"), 0 },
                                  { QStringLiteral("security"), QStringLiteral("auto") },
                                  { QStringLiteral("level"), 9 },
                                  { QStringLiteral("futureUser"), QStringLiteral("keep-user") } };
    const QJsonObject extraUser{ { QStringLiteral("id"), QStringLiteral("id-2") },
                                 { QStringLiteral("futureTailUser"), true } };
    const QJsonObject firstServer{ { QStringLiteral("address"), QStringLiteral("old.example") },
                                   { QStringLiteral("port"), 443 },
                                   { QStringLiteral("futureServer"), QStringLiteral("keep-server") },
                                   { QStringLiteral("users"), QJsonArray{ opaqueUser, extraUser } } };
    const QJsonObject extraServer{ { QStringLiteral("address"), QStringLiteral("tail.example") },
                                   { QStringLiteral("futureTailServer"), true } };
    const QJsonObject original{ { QStringLiteral("futureSettings"), QJsonObject{ { QStringLiteral("keep"), 1 } } },
                                { QStringLiteral("vnext"), QJsonArray{ firstServer, extraServer } } };

    const QJsonObject baseline{ { QStringLiteral("address"), QStringLiteral("old.example") },
                                { QStringLiteral("port"), 443 },
                                { QStringLiteral("users"),
                                  QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("id-1") },
                                                          { QStringLiteral("alterId"), 0 },
                                                          { QStringLiteral("security"), QStringLiteral("auto") },
                                                          { QStringLiteral("level"), 9 } } } } };

    const auto result = ApplyManagedFirstServerChanges(
        original, QStringLiteral("vnext"), baseline, baseline,
        { QStringLiteral("address"), QStringLiteral("port") }, QStringLiteral("users"),
        { QStringLiteral("id"), QStringLiteral("alterId"), QStringLiteral("security") });

    REQUIRE(result == original);
}

TEST_CASE("Managed first-server edits preserve opaque fields and array tails")
{
    const QJsonObject opaqueUser{ { QStringLiteral("id"), QStringLiteral("id-1") },
                                  { QStringLiteral("alterId"), 0 },
                                  { QStringLiteral("security"), QStringLiteral("auto") },
                                  { QStringLiteral("level"), 9 },
                                  { QStringLiteral("futureUser"), QStringLiteral("keep-user") } };
    const QJsonObject extraUser{ { QStringLiteral("id"), QStringLiteral("id-2") },
                                 { QStringLiteral("futureTailUser"), true } };
    const QJsonObject firstServer{ { QStringLiteral("address"), QStringLiteral("old.example") },
                                   { QStringLiteral("port"), 443 },
                                   { QStringLiteral("futureServer"), QStringLiteral("keep-server") },
                                   { QStringLiteral("users"), QJsonArray{ opaqueUser, extraUser } } };
    const QJsonObject extraServer{ { QStringLiteral("address"), QStringLiteral("tail.example") },
                                   { QStringLiteral("futureTailServer"), true } };
    const QJsonObject original{ { QStringLiteral("futureSettings"), QJsonObject{ { QStringLiteral("keep"), 1 } } },
                                { QStringLiteral("vnext"), QJsonArray{ firstServer, extraServer } } };

    const QJsonObject baseline{ { QStringLiteral("address"), QStringLiteral("old.example") },
                                { QStringLiteral("port"), 443 },
                                { QStringLiteral("users"),
                                  QJsonArray{ QJsonObject{ { QStringLiteral("id"), QStringLiteral("id-1") },
                                                          { QStringLiteral("alterId"), 0 },
                                                          { QStringLiteral("security"), QStringLiteral("auto") },
                                                          { QStringLiteral("level"), 9 } } } } };
    auto edited = baseline;
    edited.insert(QStringLiteral("address"), QStringLiteral("new.example"));
    auto editedUsers = edited.value(QStringLiteral("users")).toArray();
    auto editedFirstUser = editedUsers.first().toObject();
    editedFirstUser.insert(QStringLiteral("id"), QStringLiteral("new-id"));
    editedUsers[0] = editedFirstUser;
    edited.insert(QStringLiteral("users"), editedUsers);

    const auto result = ApplyManagedFirstServerChanges(
        original, QStringLiteral("vnext"), baseline, edited,
        { QStringLiteral("address"), QStringLiteral("port") }, QStringLiteral("users"),
        { QStringLiteral("id"), QStringLiteral("alterId"), QStringLiteral("security") });

    REQUIRE(result.value(QStringLiteral("futureSettings")) == original.value(QStringLiteral("futureSettings")));
    const auto resultServers = result.value(QStringLiteral("vnext")).toArray();
    REQUIRE(resultServers.size() == 2);
    REQUIRE(resultServers.at(1) == extraServer);

    const auto resultFirstServer = resultServers.first().toObject();
    REQUIRE(resultFirstServer.value(QStringLiteral("address")) == QStringLiteral("new.example"));
    REQUIRE(resultFirstServer.value(QStringLiteral("futureServer")) == QStringLiteral("keep-server"));

    const auto resultUsers = resultFirstServer.value(QStringLiteral("users")).toArray();
    REQUIRE(resultUsers.size() == 2);
    REQUIRE(resultUsers.at(1) == extraUser);
    const auto resultFirstUser = resultUsers.first().toObject();
    REQUIRE(resultFirstUser.value(QStringLiteral("id")) == QStringLiteral("new-id"));
    REQUIRE(resultFirstUser.value(QStringLiteral("level")) == 9);
    REQUIRE(resultFirstUser.value(QStringLiteral("futureUser")) == QStringLiteral("keep-user"));
}

TEST_CASE("Clearing managed proxy authentication removes only the edited first user")
{
    const QJsonObject firstUser{ { QStringLiteral("user"), QStringLiteral("first") },
                                 { QStringLiteral("pass"), QStringLiteral("secret") },
                                 { QStringLiteral("futureUser"), QStringLiteral("do-not-copy") } };
    const QJsonObject tailUser{ { QStringLiteral("user"), QStringLiteral("tail") },
                                { QStringLiteral("pass"), QStringLiteral("tail-secret") },
                                { QStringLiteral("futureTail"), true } };
    const QJsonObject originalServer{ { QStringLiteral("address"), QStringLiteral("proxy.example") },
                                      { QStringLiteral("port"), 1080 },
                                      { QStringLiteral("users"), QJsonArray{ firstUser, tailUser } } };
    const QJsonObject original{ { QStringLiteral("servers"), QJsonArray{ originalServer } } };
    const QJsonObject baseline{ { QStringLiteral("address"), QStringLiteral("proxy.example") },
                                { QStringLiteral("port"), 1080 },
                                { QStringLiteral("users"),
                                  QJsonArray{ QJsonObject{ { QStringLiteral("user"), QStringLiteral("first") },
                                                          { QStringLiteral("pass"), QStringLiteral("secret") } } } } };
    const QJsonObject edited{ { QStringLiteral("address"), QStringLiteral("proxy.example") },
                              { QStringLiteral("port"), 1080 } };

    const auto result = ApplyManagedFirstServerChanges(
        original, QStringLiteral("servers"), baseline, edited,
        { QStringLiteral("address"), QStringLiteral("port") }, QStringLiteral("users"),
        { QStringLiteral("user"), QStringLiteral("pass") }, true);

    const auto resultUsers = result.value(QStringLiteral("servers")).toArray().first().toObject().value(QStringLiteral("users")).toArray();
    REQUIRE(resultUsers.size() == 1);
    REQUIRE(resultUsers.first() == tailUser);
    REQUIRE_FALSE(resultUsers.first().toObject().contains(QStringLiteral("futureUser")));
}

TEST_CASE("New single-server settings still emit the editor model")
{
    const QJsonObject edited{ { QStringLiteral("address"), QStringLiteral("new.example") },
                              { QStringLiteral("port"), 443 },
                              { QStringLiteral("method"), QStringLiteral("aes-256-gcm") },
                              { QStringLiteral("password"), QStringLiteral("secret") } };

    const auto result = ApplyManagedFirstServerChanges(
        QJsonObject{}, QStringLiteral("servers"), edited, edited,
        { QStringLiteral("address"), QStringLiteral("port"), QStringLiteral("method"), QStringLiteral("password") });

    REQUIRE(result.value(QStringLiteral("servers")).toArray() == QJsonArray{ edited });
}

TEST_CASE("Unsupported persisted single-server shapes fail closed")
{
    const QJsonObject original{ { QStringLiteral("futureSettings"), true },
                                { QStringLiteral("servers"), QStringLiteral("future-array-format") } };
    const QJsonObject baseline{ { QStringLiteral("address"), QStringLiteral("old.example") }, { QStringLiteral("port"), 443 } };
    auto edited = baseline;
    edited.insert(QStringLiteral("address"), QStringLiteral("new.example"));

    const auto result = ApplyManagedFirstServerChanges(
        original, QStringLiteral("servers"), baseline, edited,
        { QStringLiteral("address"), QStringLiteral("port") });

    REQUIRE(result == original);
}
