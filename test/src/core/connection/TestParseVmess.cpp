#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QUrl>
#include <QUrlQuery>
#define CATCH_CONFIG_MAIN
#include "catch.hpp"

SCENARIO("Test Parse VMess V2 url", "[ParseVMessV2]")
{
    QvTestApplication app;
    GIVEN("vmess+tcp")
    {
        QString _;
        const QString address = "42.255.255.254";
        const int alterId = 4;
        const QString uuid = "59f34e8c-f310-49b0-b240-11663e365601";
        const QString network = "tcp";
        const int port = 11451;
        const QString comment = "日本 VIP节点5 - 10Mbps带宽 苏州-日本 IPLC-CEN专线 游戏加速用 30倍流量比例 原生日本IP落地";

        WHEN("parse Qv2ray 2.5.0 generated uri")
        {
            const QString vmessString = "vmess://eyJhZGQiOiI0Mi4yNTUuMjU1LjI1NCIsImFpZCI6NCwiaWQiOiI1OWYzNGU4Yy1mMzEw"
                                        "LTQ5YjAtYjI0MC0xMTY2M2UzNjU2MDEiLCJuZXQiOiJ0Y3AiLCJwb3J0IjoxMTQ1MSwicHMiOiLm"
                                        "l6XmnKwgVklQ6IqC54K5NSAtIDEwTWJwc+W4puWuvSDoi4/lt54t5pel5pysIElQTEMtQ0VO5LiT"
                                        "57q/IOa4uOaIj+WKoOmAn+eUqCAzMOWAjea1gemHj+avlOS+iyDljp/nlJ/ml6XmnKxJUOiQveWc"
                                        "sCIsInRscyI6Im5vbmUiLCJ0eXBlIjoibm9uZSIsInYiOjJ9Cg==";

            QString commentParsed;
            const auto result = vmess::Deserialize(vmessString, &commentParsed, &_);
            INFO("Raw VMess: " << vmessString.toStdString());
            INFO("Parsed JSON: " << QJsonDocument(result).toJson().toStdString());

            const auto networkParsed = QJsonIO::GetValue(result, "outbounds", 0, "streamSettings", "network").toString();
            const auto addressParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "address").toString();
            const auto portParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "port").toInt();
            const auto idParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "users", 0, "id").toString();
            const auto alterIdParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "users", 0, "alterId").toInt();

            REQUIRE(commentParsed.toStdString() == comment.toStdString());
            REQUIRE(addressParsed.toStdString() == address.toStdString());
            REQUIRE(portParsed == port);
            REQUIRE(idParsed.toStdString() == uuid.toStdString());
            REQUIRE(alterIdParsed == alterId);
            REQUIRE(networkParsed.toStdString() == "");
        }
    }
}

SCENARIO("Test Parse VMess V1 url", "[ParseVMessV1]")
{
    QvTestApplication app;
    GIVEN("vmess+ws")
    {
        QString _;
        const QString address = "motherfucker.net";
        const int alterId = 0;
        const QString path = "/yaboviss";
        const QString network = "ws";
        const QString uuid = "40980939-f6bd-4b17-ad26-c2aed2f1b3fc";
        const QString comment = "good bye vmess v1";
        const int port = 8003;

        WHEN("parse all stringified vmess v1")
        {
            const QString vmessString = "vmess://eyJwcyI6Imdvb2QgYnllIHZtZXNzIHYxIiwiYWRkIjoibW90aGVyZnVja2VyLm5ldCIs"
                                        "InBvcnQiOiI4MDAzIiwiaWQiOiI0MDk4MDkzOS1mNmJkLTRiMTctYWQyNi1jMmFlZDJmMWIzZmMi"
                                        "LCJhaWQiOiIwIiwibmV0Ijoid3MiLCJ0eXBlIjoibm9uZSIsImhvc3QiOiIveWFib3Zpc3MiLCJ0"
                                        "bHMiOiIifQo=";

            QString commentParsed;
            const auto result = vmess::Deserialize(vmessString, &commentParsed, &_);
            INFO("Raw VMess: " << vmessString.toStdString());
            INFO("Parsed JSON: " << QJsonDocument(result).toJson().toStdString());

            const auto networkParsed = QJsonIO::GetValue(result, "outbounds", 0, "streamSettings", "network").toString();
            const auto addressParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "address").toString();
            const auto portParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "port").toInt();
            const auto idParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "users", 0, "id").toString();
            const auto alterIdParsed = QJsonIO::GetValue(result, "outbounds", 0, "settings", "vnext", 0, "users", 0, "alterId").toInt();
            const auto typeParsed = QJsonIO::GetValue(result, "outbounds", 0, "streamSettings", "tcpSettings", "header", "type").toString();
            const auto tlsParsed = QJsonIO::GetValue(result, "outbounds", 0, "streamSettings", "security").toString();

            REQUIRE(commentParsed.toStdString() == comment.toStdString());
            REQUIRE(addressParsed.toStdString() == address.toStdString());
            REQUIRE(portParsed == port);
            REQUIRE(idParsed.toStdString() == uuid.toStdString());
            REQUIRE(alterIdParsed == alterId);
            REQUIRE(networkParsed.toStdString() == network.toStdString());
        }
    }
}

TEST_CASE("Modern VMess parser does not retain query state across calls")
{
    QvTestApplication app;
    const QString uuid = "40980939-f6bd-4b17-ad26-c2aed2f1b3fc";

    QString firstAlias;
    QString firstError;
    const auto first = vmess_new::Deserialize(
        "vmess://ws:40980939-f6bd-4b17-ad26-c2aed2f1b3fc-0@example.com:443?host=first.example&path=%2Ffirst#first",
        &firstAlias, &firstError);
    REQUIRE(firstError.isEmpty());
    REQUIRE(firstAlias == "first");
    REQUIRE(QJsonIO::GetValue(first, { "outbounds", 0, "streamSettings", "wsSettings", "headers", "Host" }) == "first.example");
    REQUIRE(QJsonIO::GetValue(first, { "outbounds", 0, "streamSettings", "wsSettings", "path" }) == "/first");
    REQUIRE(QJsonIO::GetValue(first, { "outbounds", 0, "settings", "vnext", 0, "users", 0, "id" }) == uuid);

    QString secondAlias;
    QString secondError;
    const auto second = vmess_new::Deserialize(
        "vmess://ws:40980939-f6bd-4b17-ad26-c2aed2f1b3fc-0@example.net:8443?host=second.example&path=%2Fsecond#second",
        &secondAlias, &secondError);
    REQUIRE(secondError.isEmpty());
    REQUIRE(secondAlias == "second");
    REQUIRE(QJsonIO::GetValue(second, { "outbounds", 0, "streamSettings", "wsSettings", "headers", "Host" }) == "second.example");
    REQUIRE(QJsonIO::GetValue(second, { "outbounds", 0, "streamSettings", "wsSettings", "path" }) == "/second");
    REQUIRE(QJsonIO::GetValue(second, { "outbounds", 0, "settings", "vnext", 0, "address" }) == "example.net");
    REQUIRE(QJsonIO::GetValue(second, { "outbounds", 0, "settings", "vnext", 0, "port" }).toInt() == 8443);
}

TEST_CASE("Modern VMess supported transport fields round trip")
{
    QvTestApplication app;
    VMessServerObject server;
    server.address = "example.com";
    server.port = 443;
    VMessServerObject::UserObject user;
    user.id = "40980939-f6bd-4b17-ad26-c2aed2f1b3fc";
    user.alterId = 0;
    server.users << user;

    const auto roundTrip = [&server](const StreamSettingsObject &stream)
    {
        const auto link = vmess_new::Serialize(stream, server, "roundtrip");
        QString alias;
        QString error;
        const auto result = vmess_new::Deserialize(link, &alias, &error);
        INFO("Serialized link: " << link.toStdString());
        INFO("Parser error: " << error.toStdString());
        REQUIRE(error.isEmpty());
        REQUIRE(alias == "roundtrip");
        const auto outbound = result["outbounds"].toArray().first().toObject();
        return StreamSettingsObject::fromJson(outbound["streamSettings"]);
    };

    SECTION("TCP header")
    {
        StreamSettingsObject stream;
        stream.network = "tcp";
        stream.tcpSettings.header.type = "http";
        const auto parsed = roundTrip(stream);
        REQUIRE(parsed.network == "tcp");
        REQUIRE(parsed.tcpSettings.header.type == "http");
    }

    SECTION("HTTP host and path")
    {
        StreamSettingsObject stream;
        stream.network = "http";
        stream.httpSettings.host = { "h2.example.com" };
        stream.httpSettings.path = "/h2";
        const auto parsed = roundTrip(stream);
        REQUIRE(parsed.network == "http");
        REQUIRE(parsed.httpSettings.host == QList<QString>{ "h2.example.com" });
        REQUIRE(parsed.httpSettings.path == "/h2");
    }

    SECTION("WebSocket host and path")
    {
        StreamSettingsObject stream;
        stream.network = "ws";
        stream.wsSettings.headers["Host"] = "ws.example.com";
        stream.wsSettings.path = "/socket";
        const auto parsed = roundTrip(stream);
        REQUIRE(parsed.network == "ws");
        REQUIRE(parsed.wsSettings.headers["Host"] == "ws.example.com");
        REQUIRE(parsed.wsSettings.path == "/socket");
    }

    SECTION("mKCP seed and header")
    {
        StreamSettingsObject stream;
        stream.network = "kcp";
        stream.kcpSettings.seed = "round-trip-seed";
        stream.kcpSettings.header.type = "wireguard";
        const auto parsed = roundTrip(stream);
        REQUIRE(parsed.network == "kcp");
        REQUIRE(parsed.kcpSettings.seed == "round-trip-seed");
        REQUIRE(parsed.kcpSettings.header.type == "wireguard");
    }

    SECTION("QUIC security key and header")
    {
        StreamSettingsObject stream;
        stream.network = "quic";
        stream.quicSettings.security = "aes-128-gcm";
        stream.quicSettings.key = "round-trip-key";
        stream.quicSettings.header.type = "wireguard";

        const auto link = vmess_new::Serialize(stream, server, "roundtrip");
        const QUrlQuery query{ QUrl(link) };
        REQUIRE(query.queryItemValue("type") == "wireguard");
        REQUIRE(!query.hasQueryItem("headers"));

        const auto parsed = roundTrip(stream);
        REQUIRE(parsed.network == "quic");
        REQUIRE(parsed.quicSettings.security == "aes-128-gcm");
        REQUIRE(parsed.quicSettings.key == "round-trip-key");
        REQUIRE(parsed.quicSettings.header.type == "wireguard");
    }

    SECTION("gRPC service name")
    {
        StreamSettingsObject stream;
        stream.network = "grpc";
        stream.grpcSettings.serviceName = "RoundTripService";
        const auto parsed = roundTrip(stream);
        REQUIRE(parsed.network == "grpc");
        REQUIRE(parsed.grpcSettings.serviceName == "RoundTripService");
    }
}

TEST_CASE("Modern VMess accepts QUIC links emitted with the legacy headers key")
{
    QvTestApplication app;
    QString alias;
    QString error;
    const auto result = vmess_new::Deserialize(
        "vmess://quic:40980939-f6bd-4b17-ad26-c2aed2f1b3fc-0@example.com:443?security=none&headers=wireguard#legacy",
        &alias, &error);

    REQUIRE(error.isEmpty());
    REQUIRE(alias == "legacy");
    REQUIRE(QJsonIO::GetValue(result, { "outbounds", 0, "streamSettings", "quicSettings", "header", "type" }) == "wireguard");
}
