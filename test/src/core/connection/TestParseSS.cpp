#include "3rdparty/QJsonStruct/QJsonIO.hpp"
#include "Common.hpp"
#include "src/core/connection/Serialization.hpp"
#define CATCH_CONFIG_MAIN
#include "catch.hpp"
using namespace Qv2ray::core::connection::serialization;

SCENARIO("Test Parse Shadowsocks url", "[ParseSSUrl]")
{
    QvTestApplication app;
    GIVEN("A shadowsocks server object")
    {
        ShadowSocksServerObject s;
        QString err;
        QString alias = "ssurl1";
        WHEN("the url without padding")
        {
            auto c = ss::Deserialize("ss://YmYtY2ZiOnRlc3RAMTkyLjE2OC4xMDAuMTo4ODg4", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.100.1");
            REQUIRE(s.port == 8888);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("the url with padding")
        {
            auto c = ss::Deserialize("ss://YmYtY2ZiOnRlc3RAMTkyLjE2OC4xLjE6ODM4OA==", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.1.1");
            REQUIRE(s.port == 8388);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("the url with remarks")
        {
            auto c = ss::Deserialize("ss://YmYtY2ZiOnRlc3RAMTkyLjE2OC4xMDAuMTo4ODg4#example-server", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.100.1");
            REQUIRE(s.port == 8888);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("the url with remarks and padding")
        {
            auto c = ss::Deserialize("ss://YmYtY2ZiOnRlc3RAMTkyLjE2OC4xLjE6ODM4OA==#example-server", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.1.1");
            REQUIRE(s.port == 8388);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("the url with sip003 plugin")
        {
            auto c =
                ss::Deserialize("ss://YmYtY2ZiOnRlc3Q@192.168.100.1:8888/?plugin=obfs-local%3bobfs%3dhttp%3bobfs-host%3dgoogle.com", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.100.1");
            REQUIRE(s.port == 8888);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("another url with sip003 plugin")
        {
            auto c = ss::Deserialize("ss://YmYtY2ZiOnRlc3Q@192.168.1.1:8388/?plugin=obfs-local%3bobfs%3dhttp%3bobfs-host%3dgoogle.com", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.1.1");
            REQUIRE(s.port == 8388);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("the url with sip003 plugin and remarks")
        {
            auto c = ss::Deserialize(
                "ss://YmYtY2ZiOnRlc3Q@192.168.100.1:8888/?plugin=obfs-local%3bobfs%3dhttp%3bobfs-host%3dgoogle.com#example-server", &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.100.1");
            REQUIRE(s.port == 8888);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
        WHEN("another url with sip003 plugin and remarks")
        {
            auto c = ss::Deserialize("ss://YmYtY2ZiOnRlc3Q@192.168.1.1:8388/?plugin=obfs-local%3bobfs%3dhttp%3bobfs-host%3dgoogle.com#example-server",
                                     &alias, &err);
            s = ShadowSocksServerObject::fromJson(QJsonIO::GetValue(c, "outbounds", 0, "settings", "servers", 0));
            REQUIRE(s.address.toStdString() == "192.168.1.1");
            REQUIRE(s.port == 8388);
            REQUIRE(s.password.toStdString() == "test");
            REQUIRE(s.method.toStdString() == "bf-cfb");
        }
    }
}

SCENARIO("Legacy SSD share links are rejected without affecting Shadowsocks", "[ParseSSUrl][LegacySSD]")
{
    QvTestApplication app;
    QString alias = "legacy-ssd";
    QString err;
    QString group;

    WHEN("a legacy SSD link is passed through the generic share-link importer")
    {
        const auto configs = ConvertConfigFromString("ssd://legacy", &alias, &err, &group);
        REQUIRE(configs.isEmpty());
        REQUIRE(err.toStdString() == "Unsupported SSD share link format.");
    }

    WHEN("a normal Shadowsocks link is passed through the same importer")
    {
        const auto configs = ConvertConfigFromString("ss://YmYtY2ZiOnRlc3RAMTkyLjE2OC4xMDAuMTo4ODg4", &alias, &err, &group);
        REQUIRE(err.isEmpty());
        REQUIRE(configs.size() == 1);
        const auto outbound = configs.first().second.value("outbounds").toArray().first().toObject();
        REQUIRE(outbound.value("protocol").toString().toStdString() == "shadowsocks");
    }
}
