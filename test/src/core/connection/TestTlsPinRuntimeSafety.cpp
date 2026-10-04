#include "Common.hpp"
#include "catch.hpp"
#include "core/connection/TlsPinCompatibility.hpp"
#include "core/handler/KernelInstanceHandler.hpp"
#include "utils/QvHelpers.hpp"

#include <QTemporaryDir>

using namespace Qv2ray::core::connection::tls_pin;

namespace
{
    QJsonObject PinnedStream(const QString &network = "grpc")
    {
        return { { "network", network },
                 { "security", "tls" },
                 { "tlsSettings", QJsonObject{ { "pinnedPeerCertSha256", QString(64, 'a') },
                                               { "certificates", QJsonArray{ QJsonObject{ { "futureCertificateField", true } } } } } },
                 { "futureTransportField", QJsonObject{ { "keep", true } } } };
    }

    CONFIGROOT RuntimeRoot(const QJsonObject &stream)
    {
        // A secondary outbound in a complex config must be checked too.
        return CONFIGROOT(
            QJsonObject{ { "outbounds", QJsonArray{ QJsonObject{ { "protocol", "freedom" }, { "tag", "direct" } },
                                                    QJsonObject{ { "protocol", "vless" },
                                                                 { "tag", "pinned-proxy" },
                                                                 { "settings", QJsonObject{ { "address", "127.0.0.1" },
                                                                                            { "port", 443 },
                                                                                            { "id", "b0dd64e4-0fbd-4038-9139-d1f32a68a0dc" },
                                                                                            { "encryption", "none" } } },
                                                                 { "streamSettings", stream } } } },
                         { "routing", QJsonObject{ { "domainStrategy", "AsIs" }, { "rules", QJsonArray{} } } },
                         { "futureRootField", true } });
    }
} // namespace

TEST_CASE("Raw runtime TLS pins require an explicit verification name on every outbound")
{
    for (const auto &network : { "grpc", "tcp", "ws", "kcp", "xhttp" })
    {
        INFO(network);
        auto stream = PinnedStream(network);
        const auto root = RuntimeRoot(stream);
        const auto before = QJsonDocument(root).toJson(QJsonDocument::Compact);
        const auto error = ValidateRuntimeTlsPins(root);
        REQUIRE(error.has_value());
        REQUIRE(error->contains("outbounds[1]"));
        REQUIRE(error->contains("pinned-proxy"));
        REQUIRE(error->contains("serverName"));
        REQUIRE_FALSE(error->contains(QString(64, 'a')));
        REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == before);

        auto tls = stream.value("tlsSettings").toObject();
        tls["serverName"] = "  ";
        stream["tlsSettings"] = tls;
        REQUIRE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());

        for (const auto &name : { "pin.example", "127.0.0.1" })
        {
            tls["serverName"] = name;
            stream["tlsSettings"] = tls;
            REQUIRE_FALSE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());
        }
    }
}

TEST_CASE("Runtime TLS pin validation preserves inactive and unrepresentable metadata")
{
    auto stream = PinnedStream();
    auto tls = stream.value("tlsSettings").toObject();

    SECTION("empty current pin lists do not enable pinning")
    {
        tls["pinnedPeerCertSha256"] = " , \t, ";
    }
    SECTION("removed chain pin metadata does not become a current pin")
    {
        tls.remove("pinnedPeerCertSha256");
        tls["pinnedPeerCertificateChainSha256"] = QJsonArray{ QString(64, 'b') };
    }
    SECTION("non-string current values remain subject to Xray's schema validation")
    {
        tls["pinnedPeerCertSha256"] = QJsonArray{ QString(64, 'a') };
    }
    SECTION("REALITY does not activate retained TLS metadata")
    {
        stream["security"] = "reality";
        stream["realitySettings"] = QJsonObject{ { "mldsa65Verify", "opaque-pqv" } };
    }
    SECTION("unknown stream security metadata is left to Xray")
    {
        stream["security"] = "future-security";
    }

    stream["tlsSettings"] = tls;
    const auto root = RuntimeRoot(stream);
    const auto before = QJsonDocument(root).toJson(QJsonDocument::Compact);
    REQUIRE_FALSE(ValidateRuntimeTlsPins(root).has_value());
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == before);
}

TEST_CASE("The fromMitm sentinel is not an effective TLS certificate verification name")
{
    for (const auto &network : { "grpc", "hysteria" })
        for (const auto &name : { "fromMitm", "FROMMITM", "FrOmMiTm" })
        {
            INFO(network);
            INFO(name);
            auto stream = PinnedStream(network);
            auto tls = stream.value("tlsSettings").toObject();
            tls["serverName"] = name;
            stream["tlsSettings"] = tls;
            const auto root = RuntimeRoot(stream);
            const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
            const auto error = ValidateRuntimeTlsPins(root);
            REQUIRE(error.has_value());
            REQUIRE(error->contains("GHSA-5wf9-h793-w73c"));
            REQUIRE(error->contains("fromMitm"));
            REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);
        }
}

TEST_CASE("A parsed alternate TLS verification-name list satisfies the runtime pin guard")
{
    for (const auto &serverName : { "", "fromMitm", "FrOmMiTm" })
        for (const auto &names : { "pin.example", "  , pin.example, backup.example , ", " , 127.0.0.1 , " })
        {
            INFO(serverName);
            INFO(names);
            auto stream = PinnedStream();
            auto tls = stream.value("tlsSettings").toObject();
            tls["serverName"] = serverName;
            tls["verifyPeerCertByName"] = names;
            stream["tlsSettings"] = tls;
            const auto root = RuntimeRoot(stream);
            const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
            REQUIRE_FALSE(ValidateRuntimeTlsPins(root).has_value());
            REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);
        }
}

TEST_CASE("An empty or unparseable alternate verifier cannot satisfy the TLS pin guard")
{
    for (const auto &names : { QJsonValue(""), QJsonValue("  , \t, \n "), QJsonValue(QJsonArray{ "pin.example" }), QJsonValue(QJsonValue::Null) })
    {
        auto stream = PinnedStream();
        auto tls = stream.value("tlsSettings").toObject();
        tls["serverName"] = "fromMitm";
        tls["verifyPeerCertByName"] = names;
        stream["tlsSettings"] = tls;
        REQUIRE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());
    }

    auto stream = PinnedStream();
    auto tls = stream.value("tlsSettings").toObject();
    tls["serverName"] = "pin.example";
    tls["verifyPeerCertByName"] = "  ,  ";
    stream["tlsSettings"] = tls;
    REQUIRE_FALSE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());
}

TEST_CASE("Runtime TLS pin validation follows Xray's case-insensitive JSON schema")
{
    QJsonObject tls{ { "PinnedPeerCertSha256", QString(64, 'a') } };
    QJsonObject stream{ { "Network", "GRPC" }, { "Security", "TLS" }, { "TLSSettings", tls } };
    QJsonObject outbound{ { "Protocol", "vless" }, { "StreamSettings", stream } };
    QJsonObject root{ { "Outbounds", QJsonArray{ outbound } } };
    REQUIRE(ValidateRuntimeTlsPins(root).has_value());

    tls["ServerName"] = "pin.example";
    stream["TLSSettings"] = tls;
    outbound["StreamSettings"] = stream;
    root["Outbounds"] = QJsonArray{ outbound };
    const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    REQUIRE_FALSE(ValidateRuntimeTlsPins(root).has_value());
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);

    tls.remove("ServerName");
    tls["VerifyPeerCertByName"] = "  , pin.example, ";
    stream["TLSSettings"] = tls;
    outbound["StreamSettings"] = stream;
    root["Outbounds"] = QJsonArray{ outbound };
    const auto withAlternate = QJsonDocument(root).toJson(QJsonDocument::Compact);
    REQUIRE_FALSE(ValidateRuntimeTlsPins(root).has_value());
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == withAlternate);
}

TEST_CASE("Ambiguous case variants cannot bypass runtime TLS pin validation")
{
    auto stream = PinnedStream();
    auto tls = stream.value("tlsSettings").toObject();

    SECTION("Go ignores null for a duplicate string field")
    {
        tls["PinnedPeerCertSha256"] = tls.take("pinnedPeerCertSha256");
        tls["pinnedPeerCertSha256"] = QJsonValue::Null;
        stream["tlsSettings"] = tls;
    }
    SECTION("Go merges duplicate objects instead of replacing their fields")
    {
        stream["TLSSettings"] = tls;
        stream["tlsSettings"] = QJsonObject{};
    }
    SECTION("duplicate security field names must be resolved explicitly")
    {
        stream["Security"] = "tls";
        stream["security"] = QJsonValue::Null;
    }
    SECTION("alternate verifier case variants cannot hide the effective list")
    {
        tls["VerifyPeerCertByName"] = "pin.example";
        tls["verifyPeerCertByName"] = QJsonValue::Null;
        stream["tlsSettings"] = tls;
    }

    const auto root = RuntimeRoot(stream);
    const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    const auto error = ValidateRuntimeTlsPins(root);
    REQUIRE(error.has_value());
    REQUIRE(error->contains("multiple case variants"));
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);
}

TEST_CASE("Runtime TLS pin validation accepts named OpenSSL pins without normalizing raw JSON")
{
    auto stream = PinnedStream();
    auto tls = stream.value("tlsSettings").toObject();
    QStringList bytes;
    for (int index = 0; index < 32; ++index)
        bytes.append("AA");
    tls["pinnedPeerCertSha256"] = "  " + bytes.join(':') + ", " + QString(64, 'b') + "  ";
    tls["serverName"] = "pin.example";
    stream["tlsSettings"] = tls;
    const auto root = RuntimeRoot(stream);
    const auto before = QJsonDocument(root).toJson(QJsonDocument::Compact);
    REQUIRE_FALSE(ValidateRuntimeTlsPins(root).has_value());
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == before);
}

TEST_CASE("Runtime TLS pin validation checks the effective XHTTP download stream")
{
    for (const auto &field : { "xhttpSettings", "splithttpSettings" })
    {
        INFO(field);
        auto stream = PinnedStream("xhttp");
        stream["security"] = "reality";
        auto download = PinnedStream("xhttp");
        QJsonObject transport{ { "downloadSettings", download } };
        stream[field] = transport;
        auto error = ValidateRuntimeTlsPins(RuntimeRoot(stream));
        REQUIRE(error.has_value());
        REQUIRE(error->contains(QString(field) + ".downloadSettings"));

        transport["extra"] = QJsonObject{ { "downloadSettings", download } };
        stream[field] = transport;
        error = ValidateRuntimeTlsPins(RuntimeRoot(stream));
        REQUIRE(error.has_value());
        REQUIRE(error->contains(QString(field) + ".extra.downloadSettings"));

        // Xray replaces the top-level transport options with extra. An unused
        // unsafe top-level download must not override a safe effective one.
        auto tls = download.value("tlsSettings").toObject();
        tls["serverName"] = "download.example";
        download["tlsSettings"] = tls;
        transport["extra"] = QJsonObject{ { "downloadSettings", download } };
        stream[field] = transport;
        REQUIRE_FALSE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());

        transport["extra"] = QJsonObject{ { "futureTransportOption", true } };
        stream[field] = transport;
        REQUIRE_FALSE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());

        // Similarly shaped opaque metadata outside an active XHTTP stream
        // must never be interpreted as an actual download connection.
        transport.remove("extra");
        stream[field] = transport;
        stream["network"] = "tcp";
        REQUIRE_FALSE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());
    }
}

TEST_CASE("Runtime TLS pin validation checks nested and mixed-case XHTTP downloads")
{
    const QJsonObject unsafeDownload{ { "Network", "XHTTP" },
                                      { "Security", "TLS" },
                                      { "TLSSettings", QJsonObject{ { "PinnedPeerCertSha256", QString(64, 'a') } } } };
    auto download = PinnedStream("xhttp");
    auto tls = download.value("tlsSettings").toObject();
    tls["serverName"] = "first-download.example";
    download["tlsSettings"] = tls;
    download["XHTTPSettings"] = QJsonObject{ { "DownloadSettings", unsafeDownload } };
    QJsonObject stream{ { "Network", "XHTTP" },
                        { "SplitHTTPSettings", QJsonObject{ { "Extra", QJsonObject{ { "DownloadSettings", download } } } } } };
    const auto root = RuntimeRoot(stream);
    const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    const auto error = ValidateRuntimeTlsPins(root);
    REQUIRE(error.has_value());
    REQUIRE(error->contains("splithttpSettings.extra.downloadSettings.xhttpSettings.downloadSettings"));
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);

    // xhttpSettings takes precedence over the older splithttpSettings alias.
    stream["XHTTPSettings"] = QJsonObject{};
    REQUIRE_FALSE(ValidateRuntimeTlsPins(RuntimeRoot(stream)).has_value());
}

TEST_CASE("Nested XHTTP TLS pins share the sentinel and alternate-name safety rule")
{
    auto download = PinnedStream("xhttp");
    auto tls = download.value("tlsSettings").toObject();
    tls["serverName"] = "FrOmMiTm";
    download["tlsSettings"] = tls;
    QJsonObject stream{ { "network", "xhttp" }, { "xhttpSettings", QJsonObject{ { "extra", QJsonObject{ { "downloadSettings", download } } } } } };
    const auto error = ValidateRuntimeTlsPins(RuntimeRoot(stream));
    REQUIRE(error.has_value());
    REQUIRE(error->contains("xhttpSettings.extra.downloadSettings"));

    tls["verifyPeerCertByName"] = " , download.example , ";
    download["tlsSettings"] = tls;
    stream["xhttpSettings"] = QJsonObject{ { "extra", QJsonObject{ { "downloadSettings", download } } } };
    const auto root = RuntimeRoot(stream);
    const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    REQUIRE_FALSE(ValidateRuntimeTlsPins(root).has_value());
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);
}

TEST_CASE("Kernel launch rejects unsafe raw TLS pins before replacing the generated config")
{
    QvTestApplication app;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    app.ConfigPath = directory.path() + "/";
    app.StartupArguments = {};
    GlobalConfig.kernelConfig.KernelPath(directory.filePath("missing-xray"));
    GlobalConfig.kernelConfig.AssetsPath(directory.path());

    const auto generated = directory.filePath("generated/config.gen.json");
    const QString previous = "previous generated config\r\n";
    REQUIRE(StringToFile(previous, generated));
    auto stream = PinnedStream();
    SECTION("missing server name")
    {
    }
    SECTION("fromMitm server-name sentinel")
    {
        auto tls = stream.value("tlsSettings").toObject();
        tls["serverName"] = "FrOmMiTm";
        stream["tlsSettings"] = tls;
    }
    const auto root = RuntimeRoot(stream);
    const auto original = QJsonDocument(root).toJson(QJsonDocument::Compact);
    V2RayKernelInstance instance;
    int errors = 0;
    QObject::connect(&instance, &V2RayKernelInstance::OnProcessErrored, [&errors](const QString &) { ++errors; });

    const auto error = instance.StartConnection(root);
    REQUIRE(error.has_value());
    REQUIRE(error->contains("GHSA-5wf9-h793-w73c"));
    REQUIRE_FALSE(instance.IsKernelRunning());
    REQUIRE(errors == 0);
    REQUIRE(StringFromFile(generated) == previous);
    REQUIRE(QJsonDocument(root).toJson(QJsonDocument::Compact) == original);
}

TEST_CASE("An alternate TLS verifier reaches kernel validation without losing pin settings")
{
    QvTestApplication app;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    app.ConfigPath = directory.path() + "/";
    app.StartupArguments = {};
    GlobalConfig.kernelConfig.KernelPath(directory.filePath("missing-xray"));
    GlobalConfig.kernelConfig.AssetsPath(directory.path());
    auto stream = PinnedStream();
    auto tls = stream.value("tlsSettings").toObject();
    tls["verifyPeerCertByName"] = " , pin.example , ";
    stream["tlsSettings"] = tls;
    const auto root = RuntimeRoot(stream);
    V2RayKernelInstance instance;
    const auto error = instance.StartConnection(root);
    REQUIRE(error.has_value());
    REQUIRE_FALSE(error->contains("GHSA-5wf9-h793-w73c"));
    const auto generated = ReadJsonObjectFile(directory.filePath("generated/config.gen.json"));
    REQUIRE(generated.status == Qv2ray::common::JsonObjectFileStatus::Valid);
    REQUIRE(generated.object == root);
    REQUIRE_FALSE(instance.IsKernelRunning());
}

TEST_CASE("Connection handler rejects unsafe TLS pins before dispatch or connection state changes")
{
    QvTestApplication app;
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    app.ConfigPath = directory.path() + "/";
    app.StartupArguments = {};
    QvPluginHost host;
    QScopedValueRollback<QvPluginHost *> pluginHost(PluginHost, &host);
    QScopedValueRollback<const Qv2ray::core::handler::KernelInstanceHandler *> kernelInstance(Qv2ray::core::handler::KernelInstance);

    {
        Qv2ray::core::handler::KernelInstanceHandler handler;
        int connected = 0;
        int disconnected = 0;
        QObject::connect(&handler, &Qv2ray::core::handler::KernelInstanceHandler::OnConnected,
                         [&connected](const ConnectionGroupPair &) { ++connected; });
        QObject::connect(&handler, &Qv2ray::core::handler::KernelInstanceHandler::OnDisconnected,
                         [&disconnected](const ConnectionGroupPair &) { ++disconnected; });

        // No ConnectionManager is initialized. A regression that reaches
        // GetDisplayName/Connecting dispatch would fail instead of rejecting
        // the unsafe JSON at the handler's entry boundary.
        const auto error = handler.StartConnection({}, RuntimeRoot(PinnedStream()));
        REQUIRE(error.has_value());
        REQUIRE(error->contains("GHSA-5wf9-h793-w73c"));
        REQUIRE(handler.CurrentConnection().isEmpty());
        REQUIRE(handler.ActivePluginKernelsCount() == 0);
        REQUIRE(handler.GetCurrentConnectionInboundInfo().isEmpty());
        REQUIRE(connected == 0);
        REQUIRE(disconnected == 0);
        REQUIRE_FALSE(QFile::exists(directory.filePath("generated/config.gen.json")));
    }
}
