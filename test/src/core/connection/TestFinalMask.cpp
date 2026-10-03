#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "VLESSOutboundSerializerTestHelper.hpp"
#include "src/core/connection/ConnectionIO.hpp"
#include "src/core/connection/Generation.hpp"
#include "src/core/connection/Serialization.hpp"

#include <QJsonDocument>
#include <QTemporaryFile>
#include <QUrlQuery>

#include "catch.hpp"

namespace
{
    QJsonObject ExpectedFinalMask()
    {
        const QJsonObject nested{ { "enabled", true },
                                  { "count", 7 },
                                  { "ratio", 1.25 },
                                  { "literalEscape", "%2F" },
                                  { "unicode", QString::fromUtf8("测试 ✓") } };
        const QJsonObject settings{ { "packets", "tlshello" },
                                    { "lengths", QJsonArray{ "3-5", "6-8", "10-20" } },
                                    { "delays", QJsonArray{ "10-20" } },
                                    { "futureNested", nested } };
        const QJsonObject fragment{ { "type", "fragment" }, { "settings", settings } };
        return QJsonObject{ { "tcp", QJsonArray{ fragment } },
                            { "futureTopLevel", QJsonObject{ { "flag", false }, { "value", 42 } } } };
    }

    QString BuildFinalMaskLink(const QString &rawFinalMask)
    {
        return QStringLiteral("vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=tcp&security=none&fm=") + rawFinalMask +
               QStringLiteral("#FinalMask");
    }
}

TEST_CASE("VLESS FinalMask share links preserve opaque settings")
{
    QvTestApplication app;

    const auto expected = ExpectedFinalMask();
    const auto compact = QString::fromUtf8(QJsonDocument(expected).toJson(QJsonDocument::Compact));
    const auto input = BuildFinalMaskLink(QString::fromLatin1(QUrl::toPercentEncoding(compact)));

    QString alias;
    QString error;
    const auto result = vless::Deserialize(input, &alias, &error);
    REQUIRE(error.isEmpty());
    REQUIRE(alias == "FinalMask");

    const auto outbound = result["outbounds"].toArray().first().toObject();
    const auto rawStream = outbound["streamSettings"].toObject();
    REQUIRE(rawStream.value("finalmask").toObject() == expected);

    const auto normalized = StreamSettingsObject::fromJson(rawStream);
    REQUIRE(normalized.finalmask == expected);

    const auto persisted = normalized.toJson();
    REQUIRE(persisted.value("finalmask").toObject() == expected);
    const auto reloaded = StreamSettingsObject::fromJson(persisted);
    REQUIRE(reloaded.finalmask == expected);

    const auto exported = SerializeVLESSOutboundForTest(alias, outbound["settings"].toObject(), persisted);
    REQUIRE(exported.contains("fm="));
    REQUIRE(exported.contains("%252F"));
    const QUrlQuery exportedQuery{ QUrl(exported) };
    const auto exportedDocument = QJsonDocument::fromJson(exportedQuery.queryItemValue("fm", QUrl::FullyDecoded).toUtf8());
    REQUIRE(exportedDocument.isObject());
    REQUIRE(exportedDocument.object() == expected);

    QString secondAlias;
    QString secondError;
    const auto secondResult = vless::Deserialize(exported, &secondAlias, &secondError);
    REQUIRE(secondError.isEmpty());
    REQUIRE(secondAlias == alias);
    REQUIRE(QJsonIO::GetValue(secondResult, { "outbounds", 0, "streamSettings", "finalmask" }).toObject() == expected);
}

TEST_CASE("VLESS FinalMask is disabled when fm is missing")
{
    QvTestApplication app;

    QString alias;
    QString error;
    const auto result = vless::Deserialize(
        "vless://b0dd64e4-0fbd-4038-9139-d1f32a68a0dc@example.com:443?type=tcp#NoFinalMask", &alias, &error);
    REQUIRE(error.isEmpty());

    const auto stream = StreamSettingsObject::fromJson(QJsonIO::GetValue(result, { "outbounds", 0, "streamSettings" }));
    REQUIRE(stream.finalmask.isEmpty());
    REQUIRE_FALSE(stream.toJson().contains("finalmask"));
}

TEST_CASE("VLESS FinalMask import fails closed for invalid fm")
{
    QvTestApplication app;

    const QList<QString> invalidValues{
        QStringLiteral("%7B%22tcp%22%3A"),
        QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("[]"))),
        QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("\"text\""))),
        QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("42"))),
        QStringLiteral("%ZZ"),
        QStringLiteral("%2"),
        QStringLiteral("%7B%22x%22%3A%22%FF%22%7D")
    };

    for (const auto &fm : invalidValues)
    {
        QString alias;
        QString error;
        const auto result = vless::Deserialize(BuildFinalMaskLink(fm), &alias, &error);
        REQUIRE(result.isEmpty());
        REQUIRE_FALSE(error.isEmpty());
    }
}

TEST_CASE("FinalMask runtime generation preserves transport security and opaque JSON")
{
    QJsonObject settings;
    QJsonIO::SetValue(settings, "198.51.100.1", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    StreamSettingsObject stream;
    stream.network = "xhttp";
    stream.security = "reality";
    stream.realitySettings.serverName = "reality.example.com";
    stream.realitySettings.password = "44SnKuqOTdFW80xXTKNzb-EDSVln7KRa6ziLFXbvhV0";
    stream.realitySettings.shortId = "0123456789abcdef";
    stream.xhttpSettings = QJsonObject{ { "host", "edge.example.com" }, { "path", "/xhttp" }, { "mode", "auto" } };
    stream.finalmask = ExpectedFinalMask();

    const auto generated = GenerateOutboundEntry("proxy", "vless", OUTBOUNDSETTING{ settings }, stream.toJson());
    const auto generatedStream = generated.value("streamSettings").toObject();
    REQUIRE(generatedStream.value("network") == "xhttp");
    REQUIRE(generatedStream.value("security") == "reality");
    REQUIRE(generatedStream.value("xhttpSettings").toObject() == stream.xhttpSettings);
    REQUIRE(generatedStream.value("realitySettings").toObject() == stream.realitySettings.toJson());
    REQUIRE(generatedStream.value("finalmask").toObject() == stream.finalmask);
}

TEST_CASE("VLESS FinalMask serializer fails closed for non-object input")
{
    QJsonObject settings;
    QJsonIO::SetValue(settings, "example.com", { "vnext", 0, "address" });
    QJsonIO::SetValue(settings, 443, { "vnext", 0, "port" });
    QJsonIO::SetValue(settings, "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc", { "vnext", 0, "users", 0, "id" });
    QJsonIO::SetValue(settings, "none", { "vnext", 0, "users", 0, "encryption" });

    const QJsonObject stream{ { "network", "tcp" }, { "finalmask", QJsonArray{} } };
    REQUIRE(SerializeVLESSOutboundForTest("bad", settings, stream) == "(Invalid FinalMask JSON object)");
}

TEST_CASE("Full config import preserves FinalMask")
{
    const auto expected = ExpectedFinalMask();
    QJsonObject outbound{ { "tag", "proxy" },
                          { "protocol", "vless" },
                          { "settings", QJsonObject{} },
                          { "streamSettings", QJsonObject{ { "network", "tcp" }, { "finalmask", expected } } } };
    const QJsonObject root{ { "outbounds", QJsonArray{ outbound } }, { "log", QJsonObject{ { "loglevel", "warning" } } } };

    QTemporaryFile file;
    REQUIRE(file.open());
    REQUIRE(file.write(QJsonDocument(root).toJson()) > 0);
    const auto path = file.fileName();
    file.close();

    const auto imported = ConvertConfigFromFile(path, false);
    REQUIRE(QJsonIO::GetValue(imported, { "outbounds", 0, "streamSettings", "finalmask" }).toObject() == expected);
}