#include "core/connection/RoutingJsonPreservation.hpp"
#include "core/handler/RouteStorage.hpp"
#include "ui/widgets/windows/GroupRoutePersistenceSafety.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "catch.hpp"

using namespace Qv2ray::core::connection::routing_json;
using namespace Qv2ray::core::handler::route_storage;
using namespace Qv2ray::ui::group_manager_safety;

namespace
{
    QByteArray readBytes(const QString &path)
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }

    void writeBytes(const QString &path, const QByteArray &content)
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(content) == content.size());
    }
} // namespace

TEST_CASE("routes.json malformed authority is preserved byte-for-byte")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath("routes.json");
    const QByteArray original("{broken\r\nopaque-bytes");
    writeBytes(path, original);

    const auto snapshot = LoadRouteStorage(path);
    REQUIRE(snapshot.state == RouteStorageState::Invalid);
    REQUIRE(snapshot.rawBytes == original);

    QString error;
    REQUIRE_FALSE(SaveRouteStorage(path, snapshot.state, QJsonObject{ { "replacement", true } }, &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(readBytes(path) == original);
}

TEST_CASE("routes.json non-object root is unsupported and preserved")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath("routes.json");
    const QByteArray original("[\n  {\"keep\": true}\n]\n");
    writeBytes(path, original);

    const auto snapshot = LoadRouteStorage(path);
    REQUIRE(snapshot.state == RouteStorageState::Invalid);
    REQUIRE_FALSE(SaveRouteStorage(path, snapshot.state, QJsonObject{}));
    REQUIRE(readBytes(path) == original);
}

TEST_CASE("routes.json unreadable authority blocks ordinary replacement")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath("routes.json");
    REQUIRE(QDir().mkpath(path));

    const auto snapshot = LoadRouteStorage(path);
    REQUIRE(snapshot.state == RouteStorageState::Unreadable);
    REQUIRE_FALSE(SaveRouteStorage(path, snapshot.state, QJsonObject{ { "replacement", true } }));
    REQUIRE(QFileInfo(path).isDir());
}

TEST_CASE("routes.json valid and missing states support normal persistence")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath("routes.json");

    const auto missing = LoadRouteStorage(path);
    REQUIRE(missing.state == RouteStorageState::Missing);
    const QJsonObject initial{ { "route-a", QJsonObject{ { "overrideRoute", false } } } };
    REQUIRE(SaveRouteStorage(path, missing.state, initial));

    const auto valid = LoadRouteStorage(path);
    REQUIRE(valid.state == RouteStorageState::Valid);
    REQUIRE(valid.object == initial);

    const QJsonObject updated{ { "route-a", QJsonObject{ { "overrideRoute", true } } } };
    REQUIRE(SaveRouteStorage(path, valid.state, updated));
    REQUIRE(LoadRouteStorage(path).object == updated);
}

TEST_CASE("routes.json save failures propagate")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const auto path = QDir(directory.path()).filePath("routes.json");
    REQUIRE(QDir().mkpath(path));

    QString error;
    REQUIRE_FALSE(SaveRouteStorage(path, RouteStorageState::Valid, QJsonObject{ { "route", true } }, &error));
    REQUIRE_FALSE(error.isEmpty());
    REQUIRE(QFileInfo(path).isDir());
}

TEST_CASE("group route editor requires both setter results before leaving current state")
{
    REQUIRE(GroupRouteSettersAccepted(true, true));
    REQUIRE_FALSE(GroupRouteSettersAccepted(false, true));
    REQUIRE_FALSE(GroupRouteSettersAccepted(true, false));
    REQUIRE_FALSE(GroupRouteSettersAccepted(false, false));
}

TEST_CASE("graphical routing no-op merge preserves modern and opaque rule fields")
{
    const QJsonObject originalRule{
        { "type", "field" },
        { "domain", QJsonArray{ "example.com" } },
        { "process", QJsonArray{ "chrome.exe" } },
        { "user", QJsonArray{ "test-user" } },
        { "localIP", QJsonArray{ "192.168.1.10" } },
        { "localPort", "1000-2000" },
        { "sourceIP", QJsonArray{ "10.0.0.0/8" } },
        { "customFutureField", QJsonObject{ { "nested", QJsonArray{ 1, 2, 3 } } } },
        { "outboundTag", "proxy" },
    };
    const QJsonObject baseline{
        { "type", "field" },
        { "domain", QJsonArray{ "example.com" } },
        { "outboundTag", "proxy" },
        { "balancerTag", "" },
        { "QV2RAY_RULE_ENABLED", true },
        { "QV2RAY_RULE_TAG", "New Rule" },
    };

    const auto merged = MergeManagedRoutingRule(originalRule, baseline, baseline);
    REQUIRE(merged == originalRule);
}

TEST_CASE("graphical routing managed edit preserves opaque predicates and nested JSON")
{
    const QJsonObject customFuture{ { "nested", QJsonArray{ 1, 2, 3 } } };
    const QJsonObject originalRule{
        { "type", "field" },
        { "domain", QJsonArray{ "example.com" } },
        { "process", QJsonArray{ "chrome.exe" } },
        { "user", QJsonArray{ "test-user" } },
        { "localIP", QJsonArray{ "192.168.1.10" } },
        { "localPort", "1000-2000" },
        { "sourceIP", QJsonArray{ "10.0.0.0/8" } },
        { "customFutureField", customFuture },
        { "outboundTag", "proxy" },
    };
    const QJsonObject baseline{
        { "type", "field" },
        { "domain", QJsonArray{ "example.com" } },
        { "outboundTag", "proxy" },
    };
    auto current = baseline;
    current["domain"] = QJsonArray{ "edited.example" };

    const auto merged = MergeManagedRoutingRule(originalRule, baseline, current);
    const QJsonArray expectedDomain{ "edited.example" };
    REQUIRE(merged.value("domain").toArray() == expectedDomain);
    REQUIRE(merged.value("process") == originalRule.value("process"));
    REQUIRE(merged.value("user") == originalRule.value("user"));
    REQUIRE(merged.value("localIP") == originalRule.value("localIP"));
    REQUIRE(merged.value("localPort") == originalRule.value("localPort"));
    REQUIRE(merged.value("sourceIP") == originalRule.value("sourceIP"));
    REQUIRE(merged.value("customFutureField").toObject() == customFuture);
}

TEST_CASE("graphical routing preserves unknown root and balancer fields")
{
    const QJsonObject originalBalancer{
        { "tag", "balance" },
        { "selector", QJsonArray{ "proxy" } },
        { "strategy", QJsonObject{ { "type", "random" }, { "futureStrategyField", QJsonObject{ { "keep", true } } } } },
        { "fallbackTag", "direct" },
    };
    const auto baselineBalancer = ManagedBalancerJson("balance", { "proxy" }, "random");
    const auto currentBalancer = ManagedBalancerJson("balance", { "proxy", "proxy-2" }, "random");
    const auto mergedBalancer = MergeManagedRoutingBalancer(originalBalancer, baselineBalancer, currentBalancer);

    REQUIRE(mergedBalancer.value("fallbackTag").toString() == "direct");
    REQUIRE(mergedBalancer.value("strategy").toObject().value("futureStrategyField").toObject().value("keep").toBool());
    const QJsonArray expectedSelector{ "proxy", "proxy-2" };
    REQUIRE(mergedBalancer.value("selector").toArray() == expectedSelector);

    const QJsonObject originalRule{ { "type", "field" }, { "domain", QJsonArray{ "example.com" } }, { "outboundTag", "proxy" } };
    const QJsonObject originalRouting{
        { "domainStrategy", "AsIs" },
        { "rules", QJsonArray{ originalRule } },
        { "balancers", QJsonArray{ originalBalancer } },
        { "customRoot", QJsonObject{ { "opaque", QJsonArray{ 1, 2 } } } },
    };

    const auto noOpRoot = MergeRoutingRoot(originalRouting, "AsIs", "AsIs", originalRouting.value("rules").toArray(),
                                            originalRouting.value("balancers").toArray());
    REQUIRE(noOpRoot == originalRouting);

    const auto editedRoot = MergeRoutingRoot(originalRouting, "AsIs", "AsIs", originalRouting.value("rules").toArray(),
                                              QJsonArray{ mergedBalancer });
    REQUIRE(editedRoot.value("customRoot") == originalRouting.value("customRoot"));
}

TEST_CASE("graphical routing fails closed on unsupported unsafe structures")
{
    QString reason;
    const QJsonObject conflictingRule{
        { "type", "field" },
        { "outboundTag", "proxy" },
        { "balancerTag", "balance" },
    };
    REQUIRE_FALSE(IsGraphicalRoutingStateSupported(QJsonObject{ { "rules", QJsonArray{ conflictingRule } } }, &reason));
    REQUIRE_FALSE(reason.isEmpty());

    reason.clear();
    REQUIRE_FALSE(IsGraphicalRoutingStateSupported(QJsonObject{ { "rules", QJsonObject{ { "not", "an array" } } } }, &reason));
    REQUIRE_FALSE(reason.isEmpty());
}

TEST_CASE("group route merge preserves opaque array element fields across modeled edits and movement")
{
    const QJsonArray originalServers{
        QJsonObject{ { "address", "1.1.1.1" }, { "skipFallback", true }, { "futureServer", "first" } },
        QJsonObject{ { "address", "8.8.8.8" }, { "futureServer", "second" } },
    };
    const QJsonArray baselineServers{
        QJsonObject{ { "address", "1.1.1.1" } },
        QJsonObject{ { "address", "8.8.8.8" } },
    };
    const QJsonObject original{ { "dnsConfig", QJsonObject{ { "servers", originalServers } } } };
    const QJsonObject baseline{ { "dnsConfig", QJsonObject{ { "servers", baselineServers } } } };

    SECTION("modeled field edit retains opaque fields on the edited element")
    {
        auto currentServers = baselineServers;
        currentServers[0] = QJsonObject{ { "address", "9.9.9.9" } };
        const QJsonObject current{ { "dnsConfig", QJsonObject{ { "servers", currentServers } } } };
        const auto mergedServers = MergeEditedRouteSettings(original, baseline, current)
                                       .toObject()
                                       .value("dnsConfig")
                                       .toObject()
                                       .value("servers")
                                       .toArray();
        REQUIRE(mergedServers[0].toObject().value("address") == "9.9.9.9");
        REQUIRE(mergedServers[0].toObject().value("skipFallback").toBool());
        REQUIRE(mergedServers[0].toObject().value("futureServer") == "first");
        REQUIRE(mergedServers[1] == originalServers[1]);
    }

    SECTION("reordered unchanged elements keep their original opaque identity")
    {
        const QJsonArray currentServers{ baselineServers[1], baselineServers[0] };
        const QJsonObject current{ { "dnsConfig", QJsonObject{ { "servers", currentServers } } } };
        const auto mergedServers = MergeEditedRouteSettings(original, baseline, current)
                                       .toObject()
                                       .value("dnsConfig")
                                       .toObject()
                                       .value("servers")
                                       .toArray();
        REQUIRE(mergedServers[0] == originalServers[1]);
        REQUIRE(mergedServers[1] == originalServers[0]);
    }

    SECTION("removed elements do not force surviving elements through typed serialization")
    {
        const QJsonArray currentServers{ baselineServers[1] };
        const QJsonObject current{ { "dnsConfig", QJsonObject{ { "servers", currentServers } } } };
        const auto mergedServers = MergeEditedRouteSettings(original, baseline, current)
                                       .toObject()
                                       .value("dnsConfig")
                                       .toObject()
                                       .value("servers")
                                       .toArray();
        REQUIRE(mergedServers == QJsonArray{ originalServers[1] });
    }

    SECTION("new elements do not inherit opaque fields from an existing element")
    {
        auto currentServers = baselineServers;
        currentServers.append(QJsonObject{ { "address", "4.4.4.4" } });
        const QJsonObject current{ { "dnsConfig", QJsonObject{ { "servers", currentServers } } } };
        const auto mergedServers = MergeEditedRouteSettings(original, baseline, current)
                                       .toObject()
                                       .value("dnsConfig")
                                       .toObject()
                                       .value("servers")
                                       .toArray();
        REQUIRE(mergedServers[0] == originalServers[0]);
        REQUIRE(mergedServers[1] == originalServers[1]);
        REQUIRE(mergedServers[2] == currentServers[2]);
    }
}
