#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "VLESSOutboundSerializerTestHelper.hpp"
#include "src/core/connection/Generation.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QJsonArray>
#include <QUrlQuery>

#include "catch.hpp"

namespace
{
    const auto VLESS_ENCRYPTION = QStringLiteral(
        "mlkem768x25519plus.native.0rtt.100-111-1111.75-0-111.50-0-3333.ptjHQxBQxTJ9MWr2cd5qWIflBSACHOevTauCQwa_71U");
}

TEST_CASE("VLESS Encryption remains an opaque client setting")
{
    QvTestApplication app;

    SECTION("Missing encryption defaults to none")
    {
        QString alias;
        QString error;
        const auto result = vless::Deserialize(
            "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?security=tls#Default", &alias, &error);

        REQUIRE(error.isEmpty());
        REQUIRE(QJsonIO::GetValue(result, { "outbounds", 0, "settings", "vnext", 0, "users", 0, "encryption" }) == "none");

        const auto outbound = result["outbounds"].toArray().first().toObject();
        const QUrlQuery exportedQuery{
            QUrl(SerializeVLESSOutboundForTest(alias, outbound["settings"].toObject(), outbound["streamSettings"].toObject()))
        };
        REQUIRE_FALSE(exportedQuery.hasQueryItem("encryption"));
    }

    SECTION("Modern encryption survives import, model normalization, export, and reimport")
    {
        QUrl input{ "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@192.0.2.1:443#VLESS%20Encryption" };
        QUrlQuery query;
        query.addQueryItem("encryption", VLESS_ENCRYPTION);
        query.addQueryItem("type", "tcp");
        query.addQueryItem("security", "tls");
        query.addQueryItem("flow", "xtls-rprx-vision");
        query.addQueryItem("sni", "example.com");
        input.setQuery(query);

        QString alias;
        QString error;
        const auto result = vless::Deserialize(input.toString(QUrl::FullyEncoded), &alias, &error);
        REQUIRE(error.isEmpty());
        REQUIRE(QJsonIO::GetValue(result, { "outbounds", 0, "settings", "vnext", 0, "users", 0, "encryption" }) == VLESS_ENCRYPTION);

        const auto outbound = result["outbounds"].toArray().first().toObject();
        const auto exported = SerializeVLESSOutboundForTest(alias, outbound["settings"].toObject(), outbound["streamSettings"].toObject());
        const QUrlQuery exportedQuery{ QUrl(exported) };
        REQUIRE(exportedQuery.queryItemValue("encryption") == VLESS_ENCRYPTION);

        QString secondAlias;
        QString secondError;
        const auto secondResult = vless::Deserialize(exported, &secondAlias, &secondError);
        REQUIRE(secondError.isEmpty());
        REQUIRE(secondAlias == alias);
        REQUIRE(QJsonIO::GetValue(secondResult, { "outbounds", 0, "settings", "vnext", 0, "users", 0, "encryption" }) == VLESS_ENCRYPTION);
    }

    SECTION("Outbound generation preserves modern encryption verbatim")
    {
        QJsonObject settings;
        QJsonIO::SetValue(settings, "192.0.2.1", { "vnext", 0, "address" });
        QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
        QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
        QJsonIO::SetValue(settings, VLESS_ENCRYPTION, { "vnext", 0, "users", 0, "encryption" });
        QJsonIO::SetValue(settings, "xtls-rprx-vision", { "vnext", 0, "users", 0, "flow" });

        const QJsonObject stream{ { "network", "tcp" }, { "security", "tls" } };
        const auto generated = GenerateOutboundEntry("proxy", "vless", OUTBOUNDSETTING{ settings }, stream);
        REQUIRE(QJsonIO::GetValue(generated, { "settings", "vnext", 0, "users", 0, "encryption" }) == VLESS_ENCRYPTION);
    }
}
