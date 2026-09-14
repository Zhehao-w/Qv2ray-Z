#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "plugins/protocols/core/OutboundHandler.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QUrlQuery>
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

TEST_CASE("Test VLESS URL Parsing")
{
    QvTestApplication app;
    QString alias;
    QString errMessage;

    SECTION("VLESSTCPXTLSSplice")
    {
        const static auto url = "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@qv2ray.net:3279?security=xtls&flow=rprx-xtls-splice#VLESSTCPXTLSSplice";

        const auto result = vless::Deserialize(url, &alias, &errMessage);

        INFO("Parsed: " << QJsonDocument(result).toJson().toStdString());
        REQUIRE(errMessage.isEmpty());
        REQUIRE(alias.toStdString() == "VLESSTCPXTLSSplice");
    }

    SECTION("ALPN Parse Test")
    {
        const static auto url = "vless://24a613c1-de83-4c63-ba73-a9d08c88fec3@qv2ray.net:13432?security=xtls&alpn=h2%2Chttp%2F1.1";

        const auto result = vless::Deserialize(url, &alias, &errMessage);

        INFO("Parsed: " << QJsonDocument(result).toJson().toStdString());
        REQUIRE(errMessage.isEmpty());

        const auto alpnField = QJsonIO::GetValue(result, "outbounds", 0, "streamSettings", "xtlsSettings", "alpn");
        REQUIRE(alpnField.isArray());

        const auto alpnArray = alpnField.toArray();
        REQUIRE(!alpnArray.empty());
        REQUIRE(alpnArray.size() == 2);

        const auto firstALPN = alpnArray.first();
        REQUIRE(firstALPN.isString());

        const auto firstALPNString = firstALPN.toString();
        REQUIRE(firstALPNString.toStdString() == "h2");

        const auto lastALPN = alpnArray.last();
        REQUIRE(lastALPN.isString());

        const auto lastALPNString = lastALPN.toString();
        REQUIRE(lastALPNString.toStdString() == "http/1.1");
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
        REQUIRE(!QJsonIO::GetValue(result, { "outbounds", 0, "streamSettings", "xtlsSettings" }).isObject());

        const auto outbound = result["outbounds"].toArray().first().toObject();
        const auto server = VLESSServerObject::fromJson(outbound["settings"].toObject()["vnext"].toArray().first().toObject());
        const auto stream = StreamSettingsObject::fromJson(outbound["streamSettings"].toObject());
        QJsonObject settings;
        settings["vnext"] = QJsonArray{ server.toJson() };
        const auto exported = BuiltinSerializer().SerializeOutbound("vless", alias, {}, settings, stream.toJson());
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
    }

    SECTION("REALITY Vision import and export round trip")
    {
        const static auto url =
            "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@192.0.2.1:443?encryption=none&type=tcp&security=reality&flow=xtls-rprx-vision&sni=example.com&fp=chrome&pbk=PUBLIC_KEY&sid=0123456789abcdef&spx=%2Fnews#REALITY%20Vision";
        const auto result = vless::Deserialize(url, &alias, &errMessage);
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

        const auto server = VLESSServerObject::fromJson(rawSettings["vnext"].toArray().first().toObject());
        const auto stream = StreamSettingsObject::fromJson(rawStream);
        QJsonObject settings;
        settings["vnext"] = QJsonArray{ server.toJson() };
        const auto runtimeStream = stream.toJson();
        REQUIRE(runtimeStream["security"] == "reality");
        REQUIRE(QJsonIO::GetValue(runtimeStream, { "realitySettings", "password" }) == "PUBLIC_KEY");
        REQUIRE(!QJsonIO::GetValue(runtimeStream, { "realitySettings", "publicKey" }).isString());

        const auto exported = BuiltinSerializer().SerializeOutbound("vless", alias, {}, settings, runtimeStream);
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

        const QUrlQuery query{ QUrl(BuiltinSerializer().SerializeOutbound("vless", "ordinary", {}, settings, stream)) };
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
}
