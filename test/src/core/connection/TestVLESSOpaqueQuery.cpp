#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "VLESSOutboundSerializerTestHelper.hpp"
#include "base/VLESSShareLinkOpaque.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>

#include "catch.hpp"

TEST_CASE("Unknown VLESS query items survive canonical import and export")
{
    QvTestApplication app;

    QUrl input{ "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@192.0.2.1:443#Opaque%20Query" };
    QUrlQuery inputQuery;
    inputQuery.addQueryItem("encryption", "none");
    inputQuery.addQueryItem("type", "tcp");
    inputQuery.addQueryItem("security", "reality");
    inputQuery.addQueryItem("flow", "xtls-rprx-vision");
    inputQuery.addQueryItem("sni", "example.com");
    inputQuery.addQueryItem("pbk", "PUBLIC_KEY");
    inputQuery.addQueryItem("futureOption", "first/value");
    inputQuery.addQueryItem("futureOption", "second value");
    inputQuery.addQueryItem("futureUnicode", QString::fromUtf8("雪/%2F"));
    input.setQuery(inputQuery);

    QString alias;
    QString error;
    QString group;
    const auto converted = ConvertConfigFromString(input.toString(QUrl::FullyEncoded), &alias, &error, &group);
    REQUIRE(error.isEmpty());
    REQUIRE(converted.size() == 1);

    const auto root = converted.first().second;
    const auto outbound = root["outbounds"].toArray().first().toObject();
    const auto stream = outbound["streamSettings"].toObject();
    const auto opaque = stream.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray();
    REQUIRE(opaque.size() == 3);
    for (const auto &item : opaque)
    {
        const auto key = item.toObject().value("key").toString();
        REQUIRE_FALSE(Qv2ray::base::vless_share::ManagedQueryKeys().contains(key));
    }

    const auto exported = SerializeVLESSOutboundForTest(alias, outbound["settings"].toObject(), stream);
    const QUrlQuery exportedQuery{ QUrl(exported) };
    REQUIRE(exportedQuery.queryItemValue("security") == "reality");
    REQUIRE(exportedQuery.queryItemValue("flow") == "xtls-rprx-vision");
    REQUIRE(exportedQuery.queryItemValue("sni") == "example.com");
    REQUIRE(exportedQuery.queryItemValue("pbk") == "PUBLIC_KEY");
    REQUIRE(exportedQuery.allQueryItemValues("futureOption") == QStringList{ "first/value", "second value" });
    REQUIRE(exportedQuery.queryItemValue("futureUnicode") == QString::fromUtf8("雪/%2F"));

    QString secondAlias;
    QString secondError;
    QString secondGroup;
    const auto reimported = ConvertConfigFromString(exported, &secondAlias, &secondError, &secondGroup);
    REQUIRE(secondError.isEmpty());
    REQUIRE(reimported.size() == 1);
    const auto secondStream = QJsonIO::GetValue(reimported.first().second, { "outbounds", 0, "streamSettings" }).toObject();
    REQUIRE(secondStream.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray() == opaque);
}

TEST_CASE("Opaque VLESS query metadata cannot override modeled fields")
{
    QJsonObject settings;
    QJsonIO::SetValue(settings, "192.0.2.1", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    QJsonObject stream{ { "network", "tcp" }, { "security", "reality" } };
    stream[Qv2ray::base::vless_share::OpaqueQueryMetadataKey()] = QJsonArray{
        QJsonObject{ { "key", "security" }, { "value", "none" } },
        QJsonObject{ { "key", "futureOption" }, { "value", "preserved" } }
    };

    const QUrlQuery query{ QUrl(SerializeVLESSOutboundForTest("opaque", settings, stream)) };
    REQUIRE(query.queryItemValue("security") == "reality");
    REQUIRE(query.queryItemValue("futureOption") == "preserved");
}

TEST_CASE("Malformed opaque VLESS query metadata fails closed on export")
{
    QJsonObject settings;
    QJsonIO::SetValue(settings, "192.0.2.1", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    QJsonObject stream{ { "network", "tcp" } };
    stream[Qv2ray::base::vless_share::OpaqueQueryMetadataKey()] =
        QJsonArray{ QJsonObject{ { "key", "futureOption" } } };

    REQUIRE(SerializeVLESSOutboundForTest("opaque", settings, stream) == "(Invalid VLESS opaque query metadata)");
}
