#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "VLESSOutboundSerializerTestHelper.hpp"
#include "src/core/connection/Generation.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QJsonDocument>
#include <QUrlQuery>

#include "catch.hpp"

namespace
{
    QJsonObject ExpectedExtra()
    {
        return QJsonObject{ { "xPaddingBytes", "100-1000" }, { "noGRPCHeader", false }, { "literalEscape", "%2F" } };
    }
}

TEST_CASE("VLESS XHTTP share links preserve transport settings")
{
    QvTestApplication app;

    const auto expectedHost = QStringLiteral("edge%2F.example.com");
    const auto expectedPath = QStringLiteral("/xhttp/%2F/api");
    const auto expectedMode = QStringLiteral("stream-up");
    const auto expectedExtraText = QString::fromUtf8(QJsonDocument(ExpectedExtra()).toJson(QJsonDocument::Compact));

    QUrl input{ "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@192.0.2.1:443#XHTTP%20TLS" };
    QUrlQuery query;
    query.addQueryItem("encryption", "none");
    query.addQueryItem("type", "xhttp");
    query.addQueryItem("security", "tls");
    query.addQueryItem("sni", "cdn.example.com");
    query.addQueryItem("host", QUrl::toPercentEncoding(expectedHost));
    query.addQueryItem("path", QUrl::toPercentEncoding(expectedPath));
    query.addQueryItem("mode", QUrl::toPercentEncoding(expectedMode));
    query.addQueryItem("extra", QUrl::toPercentEncoding(expectedExtraText));
    input.setQuery(query);

    QString alias;
    QString error;
    const auto result = vless::Deserialize(input.toString(QUrl::FullyEncoded), &alias, &error);
    REQUIRE(error.isEmpty());
    REQUIRE(alias == "XHTTP TLS");

    const auto outbound = result["outbounds"].toArray().first().toObject();
    const auto rawStream = outbound["streamSettings"].toObject();
    REQUIRE(rawStream["network"] == "xhttp");
    REQUIRE(QJsonIO::GetValue(rawStream, { "xhttpSettings", "host" }) == expectedHost);
    REQUIRE(QJsonIO::GetValue(rawStream, { "xhttpSettings", "path" }) == expectedPath);
    REQUIRE(QJsonIO::GetValue(rawStream, { "xhttpSettings", "mode" }) == expectedMode);
    REQUIRE(QJsonIO::GetValue(rawStream, { "xhttpSettings", "extra" }).toObject() == ExpectedExtra());

    const auto normalized = StreamSettingsObject::fromJson(rawStream);
    REQUIRE(normalized.network == "xhttp");
    REQUIRE(normalized.xhttpSettings.value("host") == expectedHost);
    REQUIRE(normalized.xhttpSettings.value("path") == expectedPath);
    REQUIRE(normalized.xhttpSettings.value("mode") == expectedMode);
    REQUIRE(normalized.xhttpSettings.value("extra").toObject() == ExpectedExtra());

    const auto persisted = normalized.toJson();
    const auto reloaded = StreamSettingsObject::fromJson(persisted);
    REQUIRE(reloaded.network == "xhttp");
    REQUIRE(reloaded.xhttpSettings == normalized.xhttpSettings);

    const auto exported = SerializeVLESSOutboundForTest(alias, outbound["settings"].toObject(), persisted);
    REQUIRE(exported.contains("%252F"));
    const QUrlQuery exportedQuery{ QUrl(exported) };
    REQUIRE(exportedQuery.queryItemValue("type") == "xhttp");
    REQUIRE(exportedQuery.queryItemValue("host", QUrl::FullyDecoded) == expectedHost);
    REQUIRE(exportedQuery.queryItemValue("path", QUrl::FullyDecoded) == expectedPath);
    REQUIRE(exportedQuery.queryItemValue("mode", QUrl::FullyDecoded) == expectedMode);
    const auto exportedExtra = QJsonDocument::fromJson(exportedQuery.queryItemValue("extra", QUrl::FullyDecoded).toUtf8());
    REQUIRE(exportedExtra.isObject());
    REQUIRE(exportedExtra.object() == ExpectedExtra());

    QString secondAlias;
    QString secondError;
    const auto secondResult = vless::Deserialize(exported, &secondAlias, &secondError);
    REQUIRE(secondError.isEmpty());
    REQUIRE(secondAlias == alias);
    REQUIRE(QJsonIO::GetValue(secondResult, { "outbounds", 0, "streamSettings", "network" }) == "xhttp");
    REQUIRE(QJsonIO::GetValue(secondResult, { "outbounds", 0, "streamSettings", "xhttpSettings" }).toObject() == normalized.xhttpSettings);
}

TEST_CASE("VLESS XHTTP extra rejects malformed or non-object JSON")
{
    QvTestApplication app;

    for (const auto &extra : { QStringLiteral("{broken"), QStringLiteral("[]") })
    {
        QUrl input{ "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443#Invalid%20XHTTP" };
        QUrlQuery query;
        query.addQueryItem("type", "xhttp");
        query.addQueryItem("extra", extra);
        input.setQuery(query);

        QString alias;
        QString error;
        const auto result = vless::Deserialize(input.toString(QUrl::FullyEncoded), &alias, &error);
        REQUIRE(result.isEmpty());
        REQUIRE(error.contains("Invalid XHTTP extra JSON object"));
    }
}

TEST_CASE("XHTTP runtime generation preserves the opaque settings object")
{
    QJsonObject settings;
    QJsonIO::SetValue(settings, "192.0.2.1", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    StreamSettingsObject stream;
    stream.network = "xhttp";
    stream.security = "tls";
    stream.tlsSettings.serverName = "cdn.example.com";
    stream.xhttpSettings = QJsonObject{ { "host", "edge.example.com" },
                                       { "path", "/runtime" },
                                       { "mode", "auto" },
                                       { "extra", ExpectedExtra() },
                                       { "futureOpaqueField", 7 } };

    const auto generated = GenerateOutboundEntry("proxy", "vless", OUTBOUNDSETTING{ settings }, stream.toJson());
    REQUIRE(QJsonIO::GetValue(generated, { "streamSettings", "network" }) == "xhttp");
    REQUIRE(QJsonIO::GetValue(generated, { "streamSettings", "xhttpSettings" }).toObject() == stream.xhttpSettings);
}

TEST_CASE("VLESS serializer fails closed for unknown transports and invalid XHTTP extra")
{
    QJsonObject settings;
    QJsonIO::SetValue(settings, "example.com", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    REQUIRE(SerializeVLESSOutboundForTest("bad", settings, QJsonObject{ { "network", "not-a-transport" } }) ==
            "(Unsupported VLESS transport)");

    QJsonObject stream{ { "network", "xhttp" }, { "xhttpSettings", QJsonObject{ { "extra", "not-an-object" } } } };
    REQUIRE(SerializeVLESSOutboundForTest("bad", settings, stream) == "(Invalid XHTTP extra JSON object)");
}
