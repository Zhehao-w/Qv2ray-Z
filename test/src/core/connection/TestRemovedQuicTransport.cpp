#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "src/core/connection/Serialization.hpp"

#include "catch.hpp"

TEST_CASE("Removed VLESS QUIC transport is rejected at the application share-link boundary")
{
    QvTestApplication app;

    QString alias;
    QString error;
    QString group;
    const auto legacyLink = QStringLiteral("vless") + QStringLiteral("://") +
                            QStringLiteral("b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=quic&security=tls#LegacyQUIC");
    const auto imported = ConvertConfigFromString(legacyLink, &alias, &error, &group);

    REQUIRE(imported.isEmpty());
    REQUIRE(error.contains("Unsupported VLESS transport"));
    REQUIRE(alias == "LegacyQUIC");

    CONFIGROOT root;
    QJsonObject outbound;
    outbound["protocol"] = "vless";
    QJsonIO::SetValue(outbound, "example.com", { "settings", "vnext", 0, "address" });
    QJsonIO::SetValue(outbound, 443, { "settings", "vnext", 0, "port" });
    QJsonIO::SetValue(outbound, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "settings", "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(outbound, "none", { "settings", "vnext", 0, "users", 0, "encryption" });
    outbound["streamSettings"] = QJsonObject{ { "network", "quic" }, { "security", "tls" } };
    root["outbounds"] = QJsonArray{ outbound };

    REQUIRE(ConvertConfigToString("LegacyQUIC", "test", root, false) == "(Unsupported VLESS transport)");
}

TEST_CASE("Legacy QUIC settings remain model round-trip compatible")
{
    const QJsonObject original{
        { "network", "quic" },
        { "quicSettings", QJsonObject{ { "security", "none" }, { "key", "legacy-key" }, { "header", QJsonObject{ { "type", "none" } } } } }
    };

    const auto typed = StreamSettingsObject::fromJson(original);
    const auto roundTripped = typed.toJson();

    REQUIRE(typed.network == "quic");
    REQUIRE(typed.quicSettings.security == "none");
    REQUIRE(typed.quicSettings.key == "legacy-key");
    REQUIRE(typed.quicSettings.header.type == "none");
    REQUIRE(roundTripped["network"] == "quic");
    REQUIRE(roundTripped["quicSettings"].toObject()["security"] == "none");
    REQUIRE(roundTripped["quicSettings"].toObject()["key"] == "legacy-key");
    REQUIRE(roundTripped["quicSettings"].toObject()["header"].toObject()["type"] == "none");
}
