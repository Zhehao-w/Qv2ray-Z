#include "core/connection/Generation.hpp"
#include "core/connection/OutboundEditorPersistence.hpp"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

namespace
{
    bool ContainsRuleValue(const QJsonArray &rules, const QString &field, const QString &value)
    {
        for (const auto &ruleValue : rules)
        {
            const auto rule = ruleValue.toObject();
            for (const auto &entry : rule.value(field).toArray())
            {
                if (entry.toString() == value)
                    return true;
            }
        }
        return false;
    }
} // namespace

TEST_CASE("Built-in mainland bypass uses the optimized pinned GeoIP asset")
{
    QvConfig_Route routeConfig;
    const auto routing = GenerateRoutes(true, true, true, "proxy", routeConfig);
    const auto rules = routing["rules"].toArray();

    REQUIRE(routing["domainStrategy"].toString() == "IPIfNonMatch");
    REQUIRE(ContainsRuleValue(rules, "ip", "ext:geoip-only-cn-private.dat:private"));
    REQUIRE(ContainsRuleValue(rules, "ip", "ext:geoip-only-cn-private.dat:cn"));
    REQUIRE(ContainsRuleValue(rules, "domain", "geosite:cn"));
    REQUIRE_FALSE(ContainsRuleValue(rules, "ip", "geoip:private"));
    REQUIRE_FALSE(ContainsRuleValue(rules, "ip", "geoip:cn"));
}

TEST_CASE("LAN and mainland bypass GeoIP rules remain independently gated")
{
    QvConfig_Route routeConfig;

    const auto mainlandOnlyRules = GenerateRoutes(true, true, false, "proxy", routeConfig)["rules"].toArray();
    REQUIRE(ContainsRuleValue(mainlandOnlyRules, "ip", "ext:geoip-only-cn-private.dat:cn"));
    REQUIRE(ContainsRuleValue(mainlandOnlyRules, "domain", "geosite:cn"));
    REQUIRE_FALSE(ContainsRuleValue(mainlandOnlyRules, "ip", "ext:geoip-only-cn-private.dat:private"));

    const auto lanOnlyRules = GenerateRoutes(true, false, true, "proxy", routeConfig)["rules"].toArray();
    REQUIRE(ContainsRuleValue(lanOnlyRules, "ip", "ext:geoip-only-cn-private.dat:private"));
    REQUIRE_FALSE(ContainsRuleValue(lanOnlyRules, "ip", "ext:geoip-only-cn-private.dat:cn"));
    REQUIRE_FALSE(ContainsRuleValue(lanOnlyRules, "domain", "geosite:cn"));
}

TEST_CASE("Advanced routing preserves full GeoIP rule strings")
{
    QvConfig_Route routeConfig;
    routeConfig.ips.direct = { "geoip:us", "geoip:jp" };

    const auto rules = GenerateRoutes(true, false, false, "proxy", routeConfig)["rules"].toArray();

    REQUIRE(ContainsRuleValue(rules, "ip", "geoip:us"));
    REQUIRE(ContainsRuleValue(rules, "ip", "geoip:jp"));
    REQUIRE_FALSE(ContainsRuleValue(rules, "ip", "ext:geoip-only-cn-private.dat:cn"));
    REQUIRE_FALSE(ContainsRuleValue(rules, "ip", "ext:geoip-only-cn-private.dat:private"));
}

TEST_CASE("Outbound editor persistence preserves unmanaged fields on same protocol and transport")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["tag"] = "old-tag";
    original["sendThrough"] = "192.0.2.44";
    original["settings"] = QJsonObject{ { "oldSetting", true } };
    original["streamSettings"] = QJsonObject{ { "network", "xhttp" },
                                               { "security", "tls" },
                                               { "futureStreamField", QJsonObject{ { "nested", 7 } } },
                                               { "finalmask", QJsonObject{ { "old", true } } } };
    original["futureOutboundField"] = QJsonArray{ "keep", 42 };
    original[QV2RAY_USE_FPROXY_KEY] = false;

    OUTBOUNDSETTING editedSettings;
    editedSettings["newSetting"] = true;
    const QJsonObject editedStream{ { "network", "xhttp" }, { "security", "reality" }, { "finalmask", QJsonObject{} } };
    auto edited = GenerateOutboundEntry("new-tag", "vless", editedSettings, editedStream);
    edited[QV2RAY_USE_FPROXY_KEY] = true;

    const auto result = PreserveUneditedOutboundFields(original, edited);
    REQUIRE(result["tag"] == "new-tag");
    REQUIRE(result["settings"].toObject() == editedSettings);
    REQUIRE(result["sendThrough"] == "192.0.2.44");
    REQUIRE(result["futureOutboundField"] == original["futureOutboundField"]);
    REQUIRE(result[QV2RAY_USE_FPROXY_KEY].toBool());

    const auto resultStream = result["streamSettings"].toObject();
    REQUIRE(resultStream["security"] == "reality");
    REQUIRE(resultStream["finalmask"].toObject().isEmpty());
    REQUIRE(resultStream["futureStreamField"] == original["streamSettings"].toObject()["futureStreamField"]);
}

TEST_CASE("Outbound editor persistence does not carry transport-specific unknown fields across a transport switch")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["sendThrough"] = "192.0.2.44";
    original["streamSettings"] = QJsonObject{ { "network", "xhttp" }, { "futureStreamField", 7 } };

    OUTBOUNDSETTING editedSettings;
    auto edited = GenerateOutboundEntry("proxy", "vless", editedSettings, QJsonObject{ { "network", "tcp" } });
    const auto result = PreserveUneditedOutboundFields(original, edited);

    REQUIRE(result["sendThrough"] == "192.0.2.44");
    REQUIRE_FALSE(result["streamSettings"].toObject().contains("futureStreamField"));
}

TEST_CASE("Outbound editor persistence preserves unmodeled fields inside active stream objects")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["streamSettings"] = QJsonObject{
        { "network", "tcp" },
        { "security", "tls" },
        { "sockopt", QJsonObject{ { "tcpFastOpen", true }, { "domainStrategy", "UseIP" }, { "tcpUserTimeout", 12000 } } },
        { "tlsSettings",
          QJsonObject{ { "serverName", "old.example.com" },
                       { "minVersion", "1.3" },
                       { "cipherSuites", "TLS_AES_128_GCM_SHA256" },
                       { "curvePreferences", QJsonArray{ "X25519MLKEM768" } },
                       { "certificates",
                         QJsonArray{ QJsonObject{ { "usage", "encipherment" },
                                                  { "certificateFile", "cert.pem" },
                                                  { "ocspStapling", 3600 },
                                                  { "buildChain", true } } } } } },
        { "tcpSettings", QJsonObject{ { "header", QJsonObject{ { "type", "none" } } }, { "acceptProxyProtocol", true } } }
    };

    const QJsonObject editedStream{
        { "network", "tcp" },
        { "security", "tls" },
        { "sockopt", QJsonObject{ { "tcpFastOpen", false } } },
        { "tlsSettings", QJsonObject{ { "serverName", "new.example.com" } } },
        { "tcpSettings", QJsonObject{ { "header", QJsonObject{ { "type", "http" } } } } }
    };
    auto edited = GenerateOutboundEntry("proxy", "vless", {}, editedStream);

    const auto resultStream = PreserveUneditedOutboundFields(original, edited)["streamSettings"].toObject();
    const auto sockopt = resultStream["sockopt"].toObject();
    REQUIRE_FALSE(sockopt["tcpFastOpen"].toBool());
    REQUIRE(sockopt["domainStrategy"] == "UseIP");
    REQUIRE(sockopt["tcpUserTimeout"] == 12000);

    const auto tls = resultStream["tlsSettings"].toObject();
    REQUIRE(tls["serverName"] == "new.example.com");
    REQUIRE(tls["minVersion"] == "1.3");
    REQUIRE(tls["cipherSuites"] == "TLS_AES_128_GCM_SHA256");
    REQUIRE(tls["curvePreferences"].toArray() == QJsonArray{ "X25519MLKEM768" });
    const auto certificates = tls["certificates"].toArray();
    REQUIRE(certificates.size() == 1);
    REQUIRE(certificates.first().toObject()["ocspStapling"] == 3600);
    REQUIRE(certificates.first().toObject()["buildChain"].toBool());

    const auto tcp = resultStream["tcpSettings"].toObject();
    REQUIRE(tcp["header"].toObject()["type"] == "http");
    REQUIRE(tcp["acceptProxyProtocol"].toBool());
}

TEST_CASE("Outbound editor persistence does not carry security-specific unknown fields across a security switch")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["streamSettings"] = QJsonObject{
        { "network", "tcp" },
        { "security", "tls" },
        { "tlsSettings", QJsonObject{ { "serverName", "old.example.com" }, { "minVersion", "1.3" } } }
    };

    const QJsonObject editedStream{
        { "network", "tcp" },
        { "security", "reality" },
        { "realitySettings", QJsonObject{ { "serverName", "new.example.com" }, { "password", "key" } } }
    };
    auto edited = GenerateOutboundEntry("proxy", "vless", {}, editedStream);

    const auto resultStream = PreserveUneditedOutboundFields(original, edited)["streamSettings"].toObject();
    REQUIRE(resultStream["security"] == "reality");
    REQUIRE_FALSE(resultStream["tlsSettings"].toObject().contains("minVersion"));
    REQUIRE(resultStream["realitySettings"].toObject()["password"] == "key");
}

TEST_CASE("Outbound editor persistence drops opaque VLESS query metadata across a security switch")
{
    OUTBOUND original;
    original["protocol"] = "vless";
    original["streamSettings"] = QJsonObject{
        { "network", "tcp" },
        { "security", "reality" },
        { Qv2ray::base::vless_share::OpaqueQueryMetadataKey(),
          QJsonArray{ QJsonObject{ { "key", "futureRealityOption" }, { "value", "keep-only-with-reality" } } } }
    };

    const QJsonObject editedStream{ { "network", "tcp" }, { "security", "tls" } };
    auto edited = GenerateOutboundEntry("proxy", "vless", {}, editedStream);
    const auto resultStream = PreserveUneditedOutboundFields(original, edited)["streamSettings"].toObject();

    REQUIRE_FALSE(resultStream.contains(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()));
}
