#include "core/connection/Generation.hpp"
#include "core/connection/OutboundEditorPersistence.hpp"
#include "core/connection/TlsPinCompatibility.hpp"

#include "catch.hpp"

using namespace Qv2ray::core::connection;
using namespace Qv2ray::core::connection::tls_pin;

namespace
{
    const QString PinA(64, 'a');
    const QString PinB(64, 'b');
    const QString LegacyPin(64, 'c');
}

TEST_CASE("current TLS pin loads into the existing editor model without exposing legacy authority")
{
    const QJsonObject originalStream{
        { "security", "tls" },
        { "tlsSettings",
          QJsonObject{ { "pinnedPeerCertSha256", PinA + ", " + PinB },
                       { "pinnedPeerCertificateChainSha256", QJsonArray{ LegacyPin } } } }
    };

    auto model = StreamSettingsObject::fromJson(originalStream);
    PrepareTlsPinEditorModel(originalStream, model);

    const QStringList expectedPins{ PinA, PinB };
    REQUIRE(model.tlsSettings.pinnedPeerCertificateChainSha256 == expectedPins);
}

TEST_CASE("TLS pin editor output is serialized using the current Xray string field")
{
    StreamSettingsObject model;
    model.security = "tls";
    model.tlsSettings.pinnedPeerCertificateChainSha256 = { PinA, PinB };

    auto editedStream = model.toJson();
    FinalizeTlsPinForXray({}, editedStream);
    const auto tls = editedStream.value("tlsSettings").toObject();

    REQUIRE(tls.value("pinnedPeerCertSha256").toString() == PinA + "," + PinB);
    REQUIRE_FALSE(tls.contains("pinnedPeerCertificateChainSha256"));
}

TEST_CASE("legacy TLS pin remains opaque on a no-op editor save")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["streamSettings"] = QJsonObject{
        { "network", "tcp" },
        { "security", "tls" },
        { "tlsSettings", QJsonObject{ { "serverName", "legacy.example" },
                                      { "pinnedPeerCertificateChainSha256", QJsonArray{ LegacyPin } } } }
    };

    const auto originalStream = original.value("streamSettings").toObject();
    auto model = StreamSettingsObject::fromJson(originalStream);
    PrepareTlsPinEditorModel(originalStream, model);
    auto editedStream = model.toJson();
    FinalizeTlsPinForXray(originalStream, editedStream);

    auto edited = GenerateOutboundEntry("proxy", "vless", OUTBOUNDSETTING{}, editedStream);
    const auto resultTls = PreserveUneditedOutboundFields(original, edited)
                               .value("streamSettings")
                               .toObject()
                               .value("tlsSettings")
                               .toObject();

    REQUIRE_FALSE(resultTls.contains("pinnedPeerCertSha256"));
    REQUIRE(resultTls.value("pinnedPeerCertificateChainSha256") == QJsonArray{ LegacyPin });
}

TEST_CASE("current TLS pin edits do not rewrite a coexisting legacy field")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["streamSettings"] = QJsonObject{
        { "network", "tcp" },
        { "security", "tls" },
        { "tlsSettings", QJsonObject{ { "pinnedPeerCertSha256", PinA },
                                      { "pinnedPeerCertificateChainSha256", QJsonArray{ LegacyPin } } } }
    };

    const auto originalStream = original.value("streamSettings").toObject();
    auto model = StreamSettingsObject::fromJson(originalStream);
    PrepareTlsPinEditorModel(originalStream, model);
    model.tlsSettings.pinnedPeerCertificateChainSha256 = { PinB };
    auto editedStream = model.toJson();
    FinalizeTlsPinForXray(originalStream, editedStream);

    auto edited = GenerateOutboundEntry("proxy", "vless", OUTBOUNDSETTING{}, editedStream);
    const auto resultTls = PreserveUneditedOutboundFields(original, edited)
                               .value("streamSettings")
                               .toObject()
                               .value("tlsSettings")
                               .toObject();

    REQUIRE(resultTls.value("pinnedPeerCertSha256").toString() == PinB);
    REQUIRE(resultTls.value("pinnedPeerCertificateChainSha256") == QJsonArray{ LegacyPin });
}

TEST_CASE("no-op current TLS pin save preserves exact persisted formatting")
{
    const auto persisted = PinA + ",  " + PinB;
    const QJsonObject originalStream{
        { "security", "tls" },
        { "tlsSettings", QJsonObject{ { "pinnedPeerCertSha256", persisted } } }
    };

    auto model = StreamSettingsObject::fromJson(originalStream);
    PrepareTlsPinEditorModel(originalStream, model);
    auto editedStream = model.toJson();
    FinalizeTlsPinForXray(originalStream, editedStream);

    REQUIRE(editedStream.value("tlsSettings").toObject().value("pinnedPeerCertSha256").toString() == persisted);
}

TEST_CASE("clearing a representable current TLS pin removes only the current field")
{
    const QJsonObject originalStream{
        { "security", "tls" },
        { "tlsSettings", QJsonObject{ { "serverName", "clear.example" }, { "pinnedPeerCertSha256", PinA } } }
    };

    auto model = StreamSettingsObject::fromJson(originalStream);
    PrepareTlsPinEditorModel(originalStream, model);
    model.tlsSettings.pinnedPeerCertificateChainSha256.clear();
    auto editedStream = model.toJson();
    FinalizeTlsPinForXray(originalStream, editedStream);

    const auto tls = editedStream.value("tlsSettings").toObject();
    REQUIRE(tls.value("serverName").toString() == "clear.example");
    REQUIRE_FALSE(tls.contains("pinnedPeerCertSha256"));
    REQUIRE_FALSE(tls.contains("pinnedPeerCertificateChainSha256"));
}

TEST_CASE("unrepresentable current TLS pin value is preserved on unrelated save")
{
    const QJsonArray unsupportedValue{ PinA };
    const QJsonObject originalStream{
        { "security", "tls" },
        { "tlsSettings", QJsonObject{ { "serverName", "opaque.example" }, { "pinnedPeerCertSha256", unsupportedValue } } }
    };

    auto model = StreamSettingsObject::fromJson(originalStream);
    PrepareTlsPinEditorModel(originalStream, model);
    auto editedStream = model.toJson();
    FinalizeTlsPinForXray(originalStream, editedStream);

    REQUIRE(editedStream.value("tlsSettings").toObject().value("pinnedPeerCertSha256") == unsupportedValue);
}

TEST_CASE("TLS pin adapter does not introduce tlsSettings for non-TLS security")
{
    QJsonObject editedStream{ { "network", "tcp" }, { "security", "reality" } };
    FinalizeTlsPinForXray({}, editedStream);
    REQUIRE_FALSE(editedStream.contains("tlsSettings"));
}
