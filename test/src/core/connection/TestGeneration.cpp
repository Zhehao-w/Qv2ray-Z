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

    auto edited = GenerateOutboundEntry("proxy", "vless", {}, QJsonObject{ { "network", "tcp" } });
    const auto result = PreserveUneditedOutboundFields(original, edited);

    REQUIRE(result["sendThrough"] == "192.0.2.44");
    REQUIRE_FALSE(result["streamSettings"].toObject().contains("futureStreamField"));
}
