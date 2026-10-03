#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "src/core/connection/Generation.hpp"
#include "src/core/connection/OutboundEditorPersistence.hpp"
#include "src/core/connection/Serialization.hpp"

#include "catch.hpp"

#include <QJsonDocument>

namespace
{
    VMessServerObject MakeVMessServer()
    {
        VMessServerObject server;
        server.address = "example.com";
        server.port = 443;
        VMessServerObject::UserObject user;
        user.id = "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc";
        user.alterId = 0;
        user.security = "auto";
        server.users << user;
        return server;
    }

    QString LegacyVMessKcpLink(const QString &headerType, bool includeSeed)
    {
        QJsonObject payload{
            { "v", 2 },
            { "ps", "LegacyKCP" },
            { "add", "example.com" },
            { "port", 443 },
            { "id", "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc" },
            { "aid", 0 },
            { "scy", "auto" },
            { "net", "kcp" },
            { "type", headerType },
            { "tls", "" }
        };
        if (includeSeed)
            payload["seed"] = "legacy-seed";
        const auto encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64();
        return QStringLiteral("vmess://") + QString::fromUtf8(encoded);
    }

    CONFIGROOT MakeExportRoot(const QString &protocol, const QString &seed, const QString &headerType)
    {
        CONFIGROOT root;
        QJsonObject outbound;
        outbound["protocol"] = protocol;
        if (protocol == "vmess")
        {
            QJsonIO::SetValue(outbound, MakeVMessServer().toJson(), { "settings", "vnext", 0 });
        }
        else
        {
            QJsonIO::SetValue(outbound, "example.com", { "settings", "vnext", 0, "address" });
            QJsonIO::SetValue(outbound, 443, { "settings", "vnext", 0, "port" });
            QJsonIO::SetValue(outbound, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "settings", "vnext", 0, "users", 0, "id" });
            QJsonIO::SetValue(outbound, "none", { "settings", "vnext", 0, "users", 0, "encryption" });
        }
        QJsonObject stream{ { "network", "kcp" } };
        QJsonIO::SetValue(stream, seed, { "kcpSettings", "seed" });
        QJsonIO::SetValue(stream, headerType, { "kcpSettings", "header", "type" });
        outbound["streamSettings"] = stream;
        root["outbounds"] = QJsonArray{ outbound };
        return root;
    }
}

TEST_CASE("Removed VLESS mKCP header and seed are rejected at the application share-link boundary")
{
    QvTestApplication app;

    for (const auto &suffix : { QStringLiteral("seed=legacy-seed"), QStringLiteral("headerType=srtp") })
    {
        QString alias;
        QString error;
        QString group;
        const auto link = QStringLiteral("vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=kcp&") + suffix +
                          QStringLiteral("#LegacyKCP");
        const auto imported = ConvertConfigFromString(link, &alias, &error, &group);
        REQUIRE(imported.isEmpty());
        REQUIRE(error.contains("Unsupported VLESS mKCP"));
    }

    QString alias;
    QString error;
    QString group;
    const auto neutral = QStringLiteral("vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=kcp&headerType=none#KCP");
    REQUIRE_FALSE(ConvertConfigFromString(neutral, &alias, &error, &group).isEmpty());
    REQUIRE(error.isEmpty());
}

TEST_CASE("Removed VMess mKCP header and seed are rejected before parser fallback")
{
    QvTestApplication app;
    const auto server = MakeVMessServer();

    StreamSettingsObject seedStream;
    seedStream.network = "kcp";
    seedStream.kcpSettings.seed = "legacy-seed";
    const auto modernSeedLink = vmess_new::Serialize(seedStream, server, "ModernKCPSeed");

    StreamSettingsObject headerStream;
    headerStream.network = "kcp";
    headerStream.kcpSettings.header.type = "srtp";
    const auto modernHeaderLink = vmess_new::Serialize(headerStream, server, "ModernKCPHeader");

    for (const auto &link : { modernSeedLink, modernHeaderLink, LegacyVMessKcpLink("srtp", false), LegacyVMessKcpLink("dtls", false),
                              LegacyVMessKcpLink("none", true) })
    {
        QString alias;
        QString error;
        QString group;
        const auto imported = ConvertConfigFromString(link, &alias, &error, &group);
        REQUIRE(imported.isEmpty());
        REQUIRE(error.contains("Unsupported VMess mKCP"));
    }
}

TEST_CASE("Removed mKCP header and seed are rejected on VLESS and VMess export")
{
    for (const auto &protocol : { QStringLiteral("vless"), QStringLiteral("vmess") })
    {
        const auto withSeed = ConvertConfigToString("LegacyKCP", "test", MakeExportRoot(protocol, "legacy-seed", "none"), false);
        const auto withHeader = ConvertConfigToString("LegacyKCP", "test", MakeExportRoot(protocol, "", "srtp"), false);
        const auto expected = protocol == "vless" ? QStringLiteral("(Unsupported VLESS mKCP header/seed)")
                                                   : QStringLiteral("(Unsupported VMess mKCP header/seed)");
        REQUIRE(withSeed == expected);
        REQUIRE(withHeader == expected);
    }
}

TEST_CASE("Runtime filtering strips removed mKCP fields without touching active settings or FinalMask")
{
    CONFIGROOT root;
    QJsonObject kcpOutbound{
        { "protocol", "vless" },
        { "streamSettings",
          QJsonObject{ { "network", "kcp" },
                       { "kcpSettings",
                         QJsonObject{ { "mtu", 1400 },
                                      { "tti", 50 },
                                      { "seed", "legacy-seed" },
                                      { "header", QJsonObject{ { "type", "srtp" } } },
                                      { "futureKcpField", 7 } } },
                       { "finalmask", QJsonObject{ { "udp", QJsonObject{ { "mode", "keep" } } } } } } }
    };
    QJsonObject tcpOutbound{
        { "protocol", "vless" },
        { "streamSettings",
          QJsonObject{ { "network", "tcp" },
                       { "kcpSettings", QJsonObject{ { "seed", "dormant" }, { "header", QJsonObject{ { "type", "srtp" } } } } } } }
    };
    root["outbounds"] = QJsonArray{ kcpOutbound, tcpOutbound };

    FillupTagsFilter(root, "outbounds");

    const auto activeStream = root["outbounds"].toArray().at(0).toObject()["streamSettings"].toObject();
    const auto activeKcp = activeStream["kcpSettings"].toObject();
    REQUIRE_FALSE(activeKcp.contains("header"));
    REQUIRE_FALSE(activeKcp.contains("seed"));
    REQUIRE(activeKcp["mtu"] == 1400);
    REQUIRE(activeKcp["tti"] == 50);
    REQUIRE(activeKcp["futureKcpField"] == 7);
    REQUIRE(activeStream["finalmask"].toObject()["udp"].toObject()["mode"] == "keep");

    const auto dormantKcp = root["outbounds"].toArray().at(1).toObject()["streamSettings"].toObject()["kcpSettings"].toObject();
    REQUIRE(dormantKcp["seed"] == "dormant");
    REQUIRE(dormantKcp["header"].toObject()["type"] == "srtp");
}

TEST_CASE("Legacy mKCP header and seed remain persisted-config compatible through editor round trip")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    const QJsonObject originalStream{
        { "network", "kcp" },
        { "kcpSettings",
          QJsonObject{ { "mtu", 1350 },
                       { "seed", "legacy-seed" },
                       { "header", QJsonObject{ { "type", "srtp" }, { "futureHeaderField", 9 } } },
                       { "futureKcpField", "keep" } } }
    };
    original["streamSettings"] = originalStream;

    auto typed = StreamSettingsObject::fromJson(originalStream);
    typed.kcpSettings.mtu = 1400;
    auto edited = GenerateOutboundEntry("proxy", "vless", OUTBOUNDSETTING{}, typed.toJson());
    const auto resultStream = PreserveUneditedOutboundFields(original, edited)["streamSettings"].toObject();
    const auto resultKcp = resultStream["kcpSettings"].toObject();

    REQUIRE(resultKcp["mtu"] == 1400);
    REQUIRE(resultKcp["seed"] == "legacy-seed");
    REQUIRE(resultKcp["header"].toObject()["type"] == "srtp");
    REQUIRE(resultKcp["header"].toObject()["futureHeaderField"] == 9);
    REQUIRE(resultKcp["futureKcpField"] == "keep");
}
