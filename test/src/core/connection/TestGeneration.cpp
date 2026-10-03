#include "core/connection/Generation.hpp"

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
