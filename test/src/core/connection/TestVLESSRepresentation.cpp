#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "VLESSOutboundSerializerTestHelper.hpp"
#include "base/VLESSSettingsCompatibility.hpp"
#include "base/VLESSShareLinkOpaque.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

#include "catch.hpp"

namespace
{
    const auto TEST_UUID = QStringLiteral("b0dd64e4-0fbd-4038-9139-d1f32a68a0dc");
    const auto UPDATED_UUID = QStringLiteral("42038cf0-7690-4a73-99d0-7e2653b9c59f");
    const auto MODERN_ENCRYPTION = QStringLiteral(
        "mlkem768x25519plus.native.0rtt.100-111-1111.75-0-111.50-0-3333.ptjHQxBQxTJ9MWr2cd5qWIflBSACHOevTauCQwa_71U");

    QJsonObject StaleVNext()
    {
        return QJsonObject{
            { "address", "stale-vnext.example" },
            { "port", 7443 },
            { "futureServerField", QJsonObject{ { "keep", true } } },
            { "users",
              QJsonArray{ QJsonObject{ { "id", "11111111-1111-1111-1111-111111111111" },
                                       { "encryption", "none" },
                                       { "flow", "xtls-rprx-vision" },
                                       { "futureUserField", "stale-but-preserved" } } } }
        };
    }
}

TEST_CASE("Flat VLESS settings remain authoritative and preserve opaque data")
{
    QJsonObject settings{
        { "address", "flat.example" },
        { "port", 443 },
        { "id", TEST_UUID },
        { "encryption", MODERN_ENCRYPTION },
        { "flow", "xtls-rprx-vision" },
        { "level", 7 },
        { "email", "flat@example.test" },
        { "futureFlatField", QJsonObject{ { "nested", QJsonArray{ 1, 2, 3 } } } },
        { "vnext", QJsonArray{ StaleVNext() } }
    };
    const auto originalVNext = settings.value("vnext");

    REQUIRE(Qv2ray::base::vless_settings::DetectRepresentation(settings) == Qv2ray::base::vless_settings::Representation::Flat);

    auto model = VLESSServerObject::fromJson(Qv2ray::base::vless_settings::ServerForEditing(settings));
    REQUIRE(model.address == "flat.example");
    REQUIRE(model.port == 443);
    REQUIRE(model.users.size() == 1);
    REQUIRE(model.users.front().id == TEST_UUID);
    REQUIRE(model.users.front().encryption == MODERN_ENCRYPTION);
    REQUIRE(model.users.front().flow == "xtls-rprx-vision");

    const auto baseline = model.toJson();
    REQUIRE(Qv2ray::base::vless_settings::ApplyManagedServerChanges(settings, baseline, model.toJson()) == settings);

    model.address = "edited-flat.example";
    model.port = 8443;
    model.users.front().id = UPDATED_UUID;
    model.users.front().encryption = "none";
    const auto edited = Qv2ray::base::vless_settings::ApplyManagedServerChanges(settings, baseline, model.toJson());

    REQUIRE(edited.value("address") == "edited-flat.example");
    REQUIRE(edited.value("port") == 8443);
    REQUIRE(edited.value("id") == UPDATED_UUID);
    REQUIRE(edited.value("encryption") == "none");
    REQUIRE(edited.value("flow") == "xtls-rprx-vision");
    REQUIRE(edited.value("level") == 7);
    REQUIRE(edited.value("email") == "flat@example.test");
    REQUIRE(edited.value("futureFlatField") == settings.value("futureFlatField"));
    REQUIRE(edited.value("vnext") == originalVNext);

    const auto info = GetVLESSOutboundHostForTest(settings);
    REQUIRE(info.first == "flat.example");
    REQUIRE(info.second == 443);

    const auto infoEdited = SetVLESSOutboundHostForTest(settings, "info-edit.example", 9443);
    REQUIRE(infoEdited.value("address") == "info-edit.example");
    REQUIRE(infoEdited.value("port") == 9443);
    REQUIRE(infoEdited.value("vnext") == originalVNext);
    REQUIRE(infoEdited.value("futureFlatField") == settings.value("futureFlatField"));
}

TEST_CASE("VNext VLESS managed edits preserve nested opaque data and array members")
{
    const QJsonObject firstUser{
        { "id", TEST_UUID },
        { "encryption", "none" },
        { "flow", "xtls-rprx-vision" },
        { "level", 9 },
        { "email", "opaque-user@example.test" },
        { "futureUserField", QJsonObject{ { "mode", "future" } } }
    };
    const QJsonObject secondUser{
        { "id", "22222222-2222-2222-2222-222222222222" },
        { "encryption", "none" },
        { "futureSecondaryUser", true }
    };
    const QJsonObject firstServer{
        { "address", "vnext.example" },
        { "port", 443 },
        { "users", QJsonArray{ firstUser, secondUser } },
        { "futureServerField", QJsonObject{ { "nested", "preserve" } } }
    };
    const QJsonObject secondServer{
        { "address", "secondary.example" },
        { "port", 1443 },
        { "users", QJsonArray{ secondUser } },
        { "futureSecondaryServer", 42 }
    };
    const QJsonObject settings{
        { "vnext", QJsonArray{ firstServer, secondServer } },
        { "futureSettingsField", QJsonObject{ { "opaque", true } } }
    };

    REQUIRE(Qv2ray::base::vless_settings::DetectRepresentation(settings) == Qv2ray::base::vless_settings::Representation::VNext);

    auto model = VLESSServerObject::fromJson(Qv2ray::base::vless_settings::ServerForEditing(settings));
    const auto baseline = model.toJson();
    REQUIRE(Qv2ray::base::vless_settings::ApplyManagedServerChanges(settings, baseline, model.toJson()) == settings);

    model.address = "edited-vnext.example";
    model.port = 8443;
    model.users.front().id = UPDATED_UUID;
    model.users.front().encryption = MODERN_ENCRYPTION;
    const auto edited = Qv2ray::base::vless_settings::ApplyManagedServerChanges(settings, baseline, model.toJson());

    REQUIRE_FALSE(edited.contains("address"));
    REQUIRE_FALSE(edited.contains("id"));
    REQUIRE(edited.value("futureSettingsField") == settings.value("futureSettingsField"));

    const auto editedVNext = edited.value("vnext").toArray();
    REQUIRE(editedVNext.size() == 2);
    REQUIRE(editedVNext.at(1).toObject() == secondServer);

    const auto editedServer = editedVNext.first().toObject();
    REQUIRE(editedServer.value("address") == "edited-vnext.example");
    REQUIRE(editedServer.value("port") == 8443);
    REQUIRE(editedServer.value("futureServerField") == firstServer.value("futureServerField"));

    const auto editedUsers = editedServer.value("users").toArray();
    REQUIRE(editedUsers.size() == 2);
    REQUIRE(editedUsers.at(1).toObject() == secondUser);
    const auto editedUser = editedUsers.first().toObject();
    REQUIRE(editedUser.value("id") == UPDATED_UUID);
    REQUIRE(editedUser.value("encryption") == MODERN_ENCRYPTION);
    REQUIRE(editedUser.value("flow") == "xtls-rprx-vision");
    REQUIRE(editedUser.value("level") == 9);
    REQUIRE(editedUser.value("email") == "opaque-user@example.test");
    REQUIRE(editedUser.value("futureUserField") == firstUser.value("futureUserField"));
}

TEST_CASE("VLESS initialization adds required defaults without normalizing persisted missing fields")
{
    VLESSServerObject newModel;
    if (newModel.users.isEmpty())
        newModel.users.push_back({});
    const auto newBaseline = newModel.toJson();
    newModel.address = "new.example";
    newModel.port = 443;
    newModel.users.front().id = TEST_UUID;

    const auto initialized = Qv2ray::base::vless_settings::ApplyManagedServerChanges({}, newBaseline, newModel.toJson());
    REQUIRE_FALSE(initialized.contains("address"));
    const auto initializedServer = initialized.value("vnext").toArray().first().toObject();
    REQUIRE(initializedServer.value("address") == "new.example");
    REQUIRE(initializedServer.value("port") == 443);
    const auto initializedUser = initializedServer.value("users").toArray().first().toObject();
    REQUIRE(initializedUser.value("id") == TEST_UUID);
    REQUIRE(initializedUser.value("encryption") == "none");

    const QJsonObject existingUser{
        { "id", TEST_UUID },
        { "futureUserField", "keep" }
    };
    const QJsonObject existingSettings{
        { "vnext",
          QJsonArray{ QJsonObject{ { "address", "existing.example" },
                                  { "port", 443 },
                                  { "users", QJsonArray{ existingUser } },
                                  { "futureServerField", true } } } }
    };
    auto existingModel = VLESSServerObject::fromJson(Qv2ray::base::vless_settings::ServerForEditing(existingSettings));
    const auto existingBaseline = existingModel.toJson();
    existingModel.address = "host-only-edit.example";
    const auto existingEdited =
        Qv2ray::base::vless_settings::ApplyManagedServerChanges(existingSettings, existingBaseline, existingModel.toJson());
    const auto preservedUser = existingEdited.value("vnext").toArray().first().toObject().value("users").toArray().first().toObject();
    REQUIRE(preservedUser.value("id") == TEST_UUID);
    REQUIRE_FALSE(preservedUser.contains("encryption"));
    REQUIRE(preservedUser.value("futureUserField") == "keep");

    const QJsonObject noUsersSettings{
        { "vnext", QJsonArray{ QJsonObject{ { "address", "no-users.example" }, { "port", 443 }, { "futureServerField", 9 } } } }
    };
    auto noUsersModel = VLESSServerObject::fromJson(Qv2ray::base::vless_settings::ServerForEditing(noUsersSettings));
    if (noUsersModel.users.isEmpty())
        noUsersModel.users.push_back({});
    const auto noUsersBaseline = noUsersModel.toJson();
    noUsersModel.address = "host-only-no-users.example";
    const auto hostOnly = Qv2ray::base::vless_settings::ApplyManagedServerChanges(noUsersSettings, noUsersBaseline, noUsersModel.toJson());
    const auto hostOnlyServer = hostOnly.value("vnext").toArray().first().toObject();
    REQUIRE(hostOnlyServer.value("address") == "host-only-no-users.example");
    REQUIRE_FALSE(hostOnlyServer.contains("users"));
    REQUIRE(hostOnlyServer.value("futureServerField") == 9);

    noUsersModel.address = "no-users.example";
    noUsersModel.users.front().id = UPDATED_UUID;
    const auto userCreated =
        Qv2ray::base::vless_settings::ApplyManagedServerChanges(noUsersSettings, noUsersBaseline, noUsersModel.toJson());
    const auto createdUser = userCreated.value("vnext").toArray().first().toObject().value("users").toArray().first().toObject();
    REQUIRE(createdUser.value("id") == UPDATED_UUID);
    REQUIRE(createdUser.value("encryption") == "none");
}

TEST_CASE("Malformed VLESS settings fail closed instead of guessing a representation")
{
    const QJsonObject malformed{
        { "vnext", "future-vnext-shape" },
        { "futureSettingsField", QJsonObject{ { "preserve", true } } }
    };
    REQUIRE(Qv2ray::base::vless_settings::DetectRepresentation(malformed) == Qv2ray::base::vless_settings::Representation::Unsupported);

    const QJsonObject pretendBaseline{ { "address", "old.example" }, { "port", 443 } };
    const QJsonObject pretendEdited{ { "address", "new.example" }, { "port", 8443 } };
    REQUIRE(Qv2ray::base::vless_settings::ApplyManagedServerChanges(malformed, pretendBaseline, pretendEdited) == malformed);
    REQUIRE(Qv2ray::base::vless_settings::SetHostAddress(malformed, "new.example", 8443) == malformed);
    REQUIRE(SerializeVLESSOutboundForTest("malformed", malformed, {}) == "(Unsupported VLESS settings representation)");

    const QJsonObject invalidFlat{
        { "address", QJsonObject{ { "future", "shape" } } },
        { "port", 443 },
        { "id", TEST_UUID },
        { "vnext", QJsonArray{ StaleVNext() } }
    };
    REQUIRE(Qv2ray::base::vless_settings::DetectRepresentation(invalidFlat) == Qv2ray::base::vless_settings::Representation::Unsupported);
    REQUIRE(Qv2ray::base::vless_settings::SetHostAddress(invalidFlat, "do-not-overwrite.example", 8443) == invalidFlat);
}

TEST_CASE("Flat VLESS settings export modern fields and opaque query data without consulting stale vnext")
{
    QvTestApplication app;

    QJsonObject settings{
        { "address", "flat-share.example" },
        { "port", 443 },
        { "id", TEST_UUID },
        { "encryption", MODERN_ENCRYPTION },
        { "flow", "xtls-rprx-vision" },
        { "futureFlatField", "preserve-in-persistence" },
        { "vnext", QJsonArray{ StaleVNext() } }
    };
    QJsonObject stream{
        { "network", "tcp" },
        { "security", "reality" },
        { "realitySettings",
          QJsonObject{ { "serverName", "reality.example" },
                       { "fingerprint", "chrome" },
                       { "password", "PUBLIC_KEY" },
                       { "shortId", "0123456789abcdef" },
                       { "mldsa65Verify", "MLDSA65_VERIFY" },
                       { "spiderX", "/" } } }
    };
    stream[Qv2ray::base::vless_share::OpaqueQueryMetadataKey()] = QJsonArray{ QStringLiteral("futureOption=preserved%2Fwire") };

    const auto exported = SerializeVLESSOutboundForTest("flat authority", settings, stream);
    REQUIRE(exported.startsWith("vless://"));

    const QUrl exportedUrl(exported);
    const QUrlQuery query(exportedUrl);
    REQUIRE(exportedUrl.host() == "flat-share.example");
    REQUIRE(exportedUrl.port() == 443);
    REQUIRE(exportedUrl.userName() == TEST_UUID);
    REQUIRE(query.queryItemValue("encryption") == MODERN_ENCRYPTION);
    REQUIRE(query.queryItemValue("flow") == "xtls-rprx-vision");
    REQUIRE(query.queryItemValue("security") == "reality");
    REQUIRE(query.queryItemValue("sni") == "reality.example");
    REQUIRE(query.queryItemValue("pbk") == "PUBLIC_KEY");
    REQUIRE(query.queryItemValue("sid") == "0123456789abcdef");
    REQUIRE(query.queryItemValue("pqv") == "MLDSA65_VERIFY");
    REQUIRE(query.queryItemValue("spx") == "/");
    REQUIRE(query.queryItemValue("futureOption") == "preserved/wire");

    QString alias;
    QString error;
    QString group;
    const auto reimported = ConvertConfigFromString(exported, &alias, &error, &group);
    REQUIRE(error.isEmpty());
    REQUIRE(reimported.size() == 1);

    const auto reimportedOutbound = reimported.first().second.value("outbounds").toArray().first().toObject();
    REQUIRE(QJsonIO::GetValue(reimportedOutbound, { "settings", "vnext", 0, "address" }) == "flat-share.example");
    REQUIRE(QJsonIO::GetValue(reimportedOutbound, { "settings", "vnext", 0, "port" }) == 443);
    REQUIRE(QJsonIO::GetValue(reimportedOutbound, { "settings", "vnext", 0, "users", 0, "id" }) == TEST_UUID);
    REQUIRE(QJsonIO::GetValue(reimportedOutbound, { "settings", "vnext", 0, "users", 0, "encryption" }) == MODERN_ENCRYPTION);
    REQUIRE(QJsonIO::GetValue(reimportedOutbound, { "settings", "vnext", 0, "users", 0, "flow" }) == "xtls-rprx-vision");
    REQUIRE(QJsonIO::GetValue(reimportedOutbound, { "streamSettings", "realitySettings", "mldsa65Verify" }) == "MLDSA65_VERIFY");
    REQUIRE(reimportedOutbound.value("streamSettings").toObject().value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray() ==
            stream.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray());
}
