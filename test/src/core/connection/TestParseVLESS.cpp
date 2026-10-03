#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "VLESSOutboundSerializerTestHelper.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QUrlQuery>
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE("Test VLESS URL Parsing")
{
    QvTestApplication app;
    QString alias;
    QString errMessage;

    SECTION("Removed legacy XTLS security is rejected")
    {
        const static auto url = "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@qv2ray.net:3279?security=xtls&flow=rprx-xtls-splice#VLESSTCPXTLSSplice";

        const auto result = vless::Deserialize(url, &alias, &errMessage);

        REQUIRE(result.isEmpty());
        REQUIRE(errMessage.contains("Unsupported VLESS stream security"));
    }

    SECTION("Removed legacy XTLS flow is rejected")
    {
        const static auto url = "vless://24a613c1-de83-4c63-ba73-a9d08c88fec3@qv2ray.net:443?security=tls&flow=xtls-rprx-splice";

        const auto result = vless::Deserialize(url, &alias, &errMessage);

        REQUIRE(result.isEmpty());
        REQUIRE(errMessage.contains("Unsupported VLESS flow"));
    }

    SECTION("Removed values are not serialized")
    {
        QJsonObject settings;
        QJsonIO::SetValue(settings, "example.com", { "vnext", 0, "address" });
        QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
        QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
        QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

        QJsonObject stream{ { "network", "tcp" }, { "security", "xtls" } };
        REQUIRE(SerializeVLESSOutboundForTest("legacy", settings, stream) == "(Unsupported VLESS stream security)");

        stream["security"] = "tls";
        QJsonIO::SetValue(settings, "xtls-rprx-direct", { "vnext", 0, "users", 0, "flow" });
        REQUIRE(SerializeVLESSOutboundForTest("legacy", settings, stream) == "(Unsupported VLESS flow)");
    }

    SECTION("gRPC Parse Test")
    {
        const static auto url = "vless://6d76fa31-8de2-40d4-8fee-6e61339c416f@qv2ray.net:123?type=grpc&security=tls&serviceName=FuckGFW&mode=multi";
        const auto result = vless::Deserialize(url, &alias, &errMessage);

        INFO("Parsed: " << QJsonDocument(result).toJson().toStdString());
        REQUIRE(errMessage.isEmpty());

        const auto grpcSettings = QJsonIO::GetValue(result, { "outbounds", 0, "streamSettings", "grpcSettings" });
        REQUIRE(grpcSettings.isObject());

        const auto grpcSettingsObj = grpcSettings.toObject();
        REQUIRE(grpcSettingsObj.contains("serviceName"));
        REQUIRE(grpcSettingsObj.contains("multiMode"));

        const auto serviceName = grpcSettingsObj["serviceName"];
        REQUIRE(serviceName.isString());

        const auto serviceNameString = serviceName.toString();
        REQUIRE(serviceNameString == "FuckGFW");

        const auto mode = grpcSettingsObj["multiMode"];
        REQUIRE(mode.isBool());

        const auto modeString = mode.toBool();
        REQUIRE(mode == true);
    }

    SECTION("TLS Vision flow is independent from legacy XTLS")
    {
        const static auto url =
            "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?encryption=none&type=tcp&security=tls&flow=xtls-rprx-vision&sni=cdn.example.com&fp=chrome#TLS%20Vision";
        const auto result = vless::Deserialize(url, &alias, &errMessage);

        REQUIRE(errMessage.isEmpty());
        REQUIRE(alias == "TLS Vision");
        REQUIRE(QJsonIO::GetValue(result, { "outbounds", 0, "streamSettings", "security" }) == "tls");
        REQUIRE(QJsonIO::GetValue(result, { "outbounds", 0, "settings", "vnext", 0, "users", 0, "flow" }) == "xtls-rprx-vision");
        REQUIRE(QJsonIO::GetValue(result, { "outbounds", 0, "streamSettings", "tlsSettings", "fingerprint" }) == "chrome");

        const auto outbound = result["outbounds"].toArray().first().toObject();
        const auto stream = StreamSettingsObject::fromJson(outbound["streamSettings"].toObject());
        const auto exported = SerializeVLESSOutboundForTest(alias, outbound["settings"].toObject(), stream.toJson());
        const QUrl exportedUrl{ exported };
        const QUrlQuery exportedQuery{ QUrl(exported) };
        REQUIRE(exportedUrl.fragment() == alias);
        REQUIRE(exportedUrl.userName() == "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc");
        REQUIRE(exportedUrl.host() == "example.com");
        REQUIRE(exportedUrl.port() == 443);
        REQUIRE(exportedQuery.queryItemValue("encryption") == "none");
        REQUIRE(exportedQuery.queryItemValue("type") == "tcp");
        REQUIRE(exportedQuery.queryItemValue("security") == "tls");
        REQUIRE(exportedQuery.queryItemValue("sni") == "cdn.example.com");
        REQUIRE(exportedQuery.queryItemValue("fp") == "chrome");
        REQUIRE(exportedQuery.queryItemValue("flow") == "xtls-rprx-vision");
        REQUIRE(!exportedQuery.hasQueryItem("pqv"));
    }

    SECTION("REALITY Vision import and export round trip")
    {
        // ML-DSA-65 public keys encode 1,952 bytes as 2,603 unpadded
        // base64url characters. This deterministic value is fake but exercises
        // the realistic length and URL-safe alphabet used by Xray.
        const auto expectedPQV = QString("Ab0_-").repeated(521).left(2603);
        QUrl inputUrl{ "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@192.0.2.1:443#REALITY%20Vision" };
        QUrlQuery inputQuery;
        inputQuery.addQueryItem("encryption", "none");
        inputQuery.addQueryItem("type", "tcp");
        inputQuery.addQueryItem("security", "reality");
        inputQuery.addQueryItem("flow", "xtls-rprx-vision");
        inputQuery.addQueryItem("sni", "example.com");
        inputQuery.addQueryItem("fp", "chrome");
        inputQuery.addQueryItem("pbk", "PUBLIC_KEY");
        inputQuery.addQueryItem("sid", "0123456789abcdef");
        inputQuery.addQueryItem("spx", "/news");
        inputQuery.addQueryItem("pqv", expectedPQV);
        inputUrl.setQuery(inputQuery);

        const auto result = vless::Deserialize(inputUrl.toString(QUrl::FullyEncoded), &alias, &errMessage);
        const auto outbound = result["outbounds"].toArray().first().toObject();
        const auto rawSettings = outbound["settings"].toObject();
        const auto rawStream = outbound["streamSettings"].toObject();

        REQUIRE(errMessage.isEmpty());
        REQUIRE(rawStream["security"] == "reality");
        REQUIRE(QJsonIO::GetValue(rawStream, { "realitySettings", "serverName" }) == "example.com");
        REQUIRE(QJsonIO::GetValue(rawStream, { "realitySettings", "fingerprint" }) == "chrome");
        REQUIRE(QJsonIO::GetValue(rawStream, { "realitySettings", "password" }) == "PUBLIC_KEY");
        REQUIRE(!QJsonIO::GetValue(rawStream, { "realitySettings", "publicKey" }).isString());
        REQUIRE(QJsonIO::GetValue(rawStream, { "realitySettings", "shortId" }) == "0123456789abcdef");
        REQUIRE(QJsonIO::GetValue(rawStream, { "realitySettings", "spiderX" }) == "/news");
        REQUIRE(QJsonIO::GetValue(rawStream, { "realitySettings", "mldsa65Verify" }) == expectedPQV);

        const auto stream = StreamSettingsObject::fromJson(rawStream);
        REQUIRE(stream.realitySettings.spiderX == "/news");
        REQUIRE(stream.realitySettings.mldsa65Verify == expectedPQV);
        const auto runtimeStream = stream.toJson();
        REQUIRE(runtimeStream["security"] == "reality");
        REQUIRE(QJsonIO::GetValue(runtimeStream, { "realitySettings", "password" }) == "PUBLIC_KEY");
        REQUIRE(!QJsonIO::GetValue(runtimeStream, { "realitySettings", "publicKey" }).isString());
        REQUIRE(QJsonIO::GetValue(runtimeStream, { "realitySettings", "mldsa65Verify" }) == expectedPQV);

        const auto exported = SerializeVLESSOutboundForTest(alias, rawSettings, runtimeStream);
        const QUrl exportedUrl{ exported };
        const QUrlQuery exportedQuery{ exportedUrl };
        REQUIRE(exportedUrl.fragment() == alias);
        REQUIRE(exportedUrl.userName() == "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc");
        REQUIRE(exportedUrl.host() == "192.0.2.1");
        REQUIRE(exportedUrl.port() == 443);
        REQUIRE(exportedQuery.queryItemValue("encryption") == "none");
        REQUIRE(exportedQuery.queryItemValue("type") == "tcp");
        REQUIRE(exportedQuery.queryItemValue("security") == "reality");
        REQUIRE(exportedQuery.queryItemValue("sni") == "example.com");
        REQUIRE(exportedQuery.queryItemValue("fp") == "chrome");
        REQUIRE(exportedQuery.queryItemValue("flow") == "xtls-rprx-vision");
        REQUIRE(exportedQuery.queryItemValue("pbk") == "PUBLIC_KEY");
        REQUIRE(exportedQuery.queryItemValue("sid") == "0123456789abcdef");
        REQUIRE(exportedQuery.queryItemValue("spx") == "/news");
        REQUIRE(exportedQuery.queryItemValue("pqv") == expectedPQV);
        REQUIRE(!exportedQuery.hasQueryItem("mldsa65Verify"));

        QString secondAlias;
        QString secondError;
        const auto secondResult = vless::Deserialize(exported, &secondAlias, &secondError);
        REQUIRE(secondError.isEmpty());
        REQUIRE(QJsonIO::GetValue(secondResult, { "outbounds", 0, "streamSettings", "realitySettings", "mldsa65Verify" }) == expectedPQV);

        auto withoutPQV = runtimeStream;
        auto realitySettings = withoutPQV["realitySettings"].toObject();
        realitySettings["mldsa65Verify"] = "";
        withoutPQV["realitySettings"] = realitySettings;
        const QUrlQuery emptyQuery{ QUrl(SerializeVLESSOutboundForTest(alias, rawSettings, withoutPQV)) };
        REQUIRE(!emptyQuery.hasQueryItem("pqv"));
        REQUIRE(emptyQuery.queryItemValue("pbk") == "PUBLIC_KEY");
        REQUIRE(emptyQuery.queryItemValue("sid") == "0123456789abcdef");
        REQUIRE(emptyQuery.queryItemValue("spx") == "/news");
    }

    SECTION("Ordinary VLESS TLS export keeps legacy default omission")
    {
        QJsonObject settings;
        QJsonIO::SetValue(settings, "example.com", { "vnext", 0, "address" });
        QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
        QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
        QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });
        QJsonObject stream;
        stream["network"] = "tcp";
        stream["security"] = "tls";

        const QUrlQuery query{ QUrl(SerializeVLESSOutboundForTest("ordinary", settings, stream)) };
        REQUIRE(!query.hasQueryItem("encryption"));
        REQUIRE(!query.hasQueryItem("type"));
        REQUIRE(!query.hasQueryItem("flow"));
        REQUIRE(query.queryItemValue("security") == "tls");
    }

    SECTION("RAW share-link spelling normalizes to TCP storage")
    {
        const static auto url = "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=raw&security=tls&flow=xtls-rprx-vision#RAW";
        const auto result = vless::Deserialize(url, &alias, &errMessage);
        const auto outbound = result["outbounds"].toArray().first().toObject();
        const auto stream = StreamSettingsObject::fromJson(outbound["streamSettings"].toObject());

        REQUIRE(errMessage.isEmpty());
        REQUIRE(stream.network == "tcp");
        REQUIRE(stream.security == "tls");
    }

    SECTION("Unsupported transports are rejected without returning a partial config")
    {
        const static auto url =
            "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=xhttp&security=reality&path=%2Fapi&host=cdn.example.com&mode=auto#Unsupported%20XHTTP";

        const auto direct = vless::Deserialize(url, &alias, &errMessage);
        REQUIRE(direct.isEmpty());
        REQUIRE(errMessage.contains("Unsupported VLESS transport"));

        alias.clear();
        errMessage.clear();
        QString groupName;
        const auto converted = ConvertConfigFromString(url, &alias, &errMessage, &groupName);
        REQUIRE(converted.isEmpty());
        REQUIRE(errMessage.contains("Unsupported VLESS transport"));
        REQUIRE(alias == "Unsupported XHTTP");
    }
}

TEST_CASE("VLESS QUIC header type round trips independently of QUIC encryption")
{
    QvTestApplication app;
    QJsonObject settings;
    QJsonIO::SetValue(settings, "example.com", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    StreamSettingsObject stream;
    stream.network = "quic";
    stream.quicSettings.security = "none";
    stream.quicSettings.header.type = "wireguard";

    const auto link = SerializeVLESSOutboundForTest("roundtrip", settings, stream.toJson());
    const QUrlQuery query{ QUrl(link) };
    INFO("Serialized link: " << link.toStdString());
    REQUIRE(query.queryItemValue("type") == "quic");
    REQUIRE(!query.hasQueryItem("quicSecurity"));
    REQUIRE(!query.hasQueryItem("key"));
    REQUIRE(query.queryItemValue("headerType") == "wireguard");

    QString alias;
    QString error;
    const auto result = vless::Deserialize(link, &alias, &error);
    REQUIRE(error.isEmpty());
    REQUIRE(alias == "roundtrip");
    const auto outbound = result["outbounds"].toArray().first().toObject();
    const auto parsed = StreamSettingsObject::fromJson(outbound["streamSettings"]);
    REQUIRE(parsed.network == "quic");
    REQUIRE(parsed.quicSettings.security == "none");
    REQUIRE(parsed.quicSettings.key.isEmpty());
    REQUIRE(parsed.quicSettings.header.type == "wireguard");
}
