#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "src/core/connection/Serialization.hpp"

#include "catch.hpp"

#include <QJsonDocument>

namespace
{
    QString LegacyVMessLinkForNetwork(const QString &network)
    {
        const QJsonObject payload{
            { "v", 2 },
            { "ps", "LegacyHTTP" },
            { "add", "example.com" },
            { "port", 443 },
            { "id", "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc" },
            { "aid", 0 },
            { "scy", "auto" },
            { "net", network },
            { "type", "none" },
            { "tls", "tls" }
        };
        const auto encoded = QJsonDocument(payload).toJson(QJsonDocument::Compact).toBase64();
        return QStringLiteral("vmess://") + QString::fromUtf8(encoded);
    }

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
}

TEST_CASE("Removed VLESS HTTP transports are rejected at the application share-link boundary")
{
    QvTestApplication app;

    for (const auto &network : { QStringLiteral("http"), QStringLiteral("h2"), QStringLiteral("h3") })
    {
        QString alias;
        QString error;
        QString group;
        const auto link = QStringLiteral("vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=") + network +
                          QStringLiteral("&security=tls#LegacyHTTP");
        const auto imported = ConvertConfigFromString(link, &alias, &error, &group);

        REQUIRE(imported.isEmpty());
        REQUIRE(error.contains("Unsupported VLESS transport"));
    }
}

TEST_CASE("Removed VMess HTTP transports are rejected without legacy h3 fallback")
{
    QvTestApplication app;

    const auto server = MakeVMessServer();
    StreamSettingsObject stream;
    stream.network = "http";
    stream.httpSettings.host << "example.com";
    stream.httpSettings.path = "/legacy";

    const auto modernLink = vmess_new::Serialize(stream, server, "ModernHTTP");
    REQUIRE_FALSE(modernLink.isEmpty());

    QString alias;
    QString error;
    QString group;
    REQUIRE(ConvertConfigFromString(modernLink, &alias, &error, &group).isEmpty());
    REQUIRE(error.contains("Unsupported VMess transport"));

    for (const auto &network : { QStringLiteral("http"), QStringLiteral("h2"), QStringLiteral("h3") })
    {
        alias.clear();
        error.clear();
        group.clear();
        const auto imported = ConvertConfigFromString(LegacyVMessLinkForNetwork(network), &alias, &error, &group);
        REQUIRE(imported.isEmpty());
        REQUIRE(error.contains("Unsupported VMess transport"));
        REQUIRE(error.contains(network));
    }
}

TEST_CASE("Removed HTTP transports are rejected on VLESS and VMess export")
{
    const auto server = MakeVMessServer();

    for (const auto &protocol : { QStringLiteral("vless"), QStringLiteral("vmess") })
    {
        for (const auto &network : { QStringLiteral("http"), QStringLiteral("h2"), QStringLiteral("h3") })
        {
            CONFIGROOT root;
            QJsonObject outbound;
            outbound["protocol"] = protocol;
            if (protocol == "vmess")
            {
                QJsonIO::SetValue(outbound, server.toJson(), { "settings", "vnext", 0 });
            }
            else
            {
                QJsonIO::SetValue(outbound, "example.com", { "settings", "vnext", 0, "address" });
                QJsonIO::SetValue(outbound, 443, { "settings", "vnext", 0, "port" });
                QJsonIO::SetValue(outbound, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "settings", "vnext", 0, "users", 0, "id" });
                QJsonIO::SetValue(outbound, "none", { "settings", "vnext", 0, "users", 0, "encryption" });
            }
            outbound["streamSettings"] = QJsonObject{ { "network", network }, { "security", "tls" } };
            root["outbounds"] = QJsonArray{ outbound };

            const auto exported = ConvertConfigToString("LegacyHTTP", "test", root, false);
            REQUIRE(exported == (protocol == "vless" ? "(Unsupported VLESS transport)" : "(Unsupported VMess transport)"));
        }
    }
}

TEST_CASE("Legacy HTTP settings remain model round-trip compatible")
{
    const QJsonObject original{
        { "network", "http" },
        { "httpSettings",
          QJsonObject{ { "host", QJsonArray{ " legacy.example.com ", "alt.example.com" } },
                       { "path", "/legacy" },
                       { "method", "GET" },
                       { "headers", QJsonObject{ { "X-Test", QJsonArray{ "one", "two" } } } } } }
    };

    const auto typed = StreamSettingsObject::fromJson(original);
    const auto roundTripped = typed.toJson();
    const auto reparsed = StreamSettingsObject::fromJson(roundTripped);

    REQUIRE(typed.network == "http");
    REQUIRE(typed.httpSettings.host == QList<QString>{ " legacy.example.com ", "alt.example.com" });
    REQUIRE(typed.httpSettings.path == "/legacy");
    REQUIRE(typed.httpSettings.method == "GET");
    REQUIRE(roundTripped["network"] == "http");
    REQUIRE(reparsed.network == "http");
    REQUIRE(reparsed.httpSettings.host == typed.httpSettings.host);
    REQUIRE(reparsed.httpSettings.path == typed.httpSettings.path);
    REQUIRE(reparsed.httpSettings.method == typed.httpSettings.method);
    REQUIRE(reparsed.httpSettings.headers == typed.httpSettings.headers);
}
