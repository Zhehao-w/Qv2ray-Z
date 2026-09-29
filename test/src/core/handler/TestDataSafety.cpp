#include "core/handler/ConfigDataSafety.hpp"
#include "utils/QvHelpers.hpp"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using Qv2ray::common::JsonObjectFileStatus;
using Qv2ray::core::handler::data_safety::BuildSubscriptionMembership;

TEST_CASE("Atomic text writes report success and failure truthfully")
{
    QTemporaryDir temporaryRoot;
    REQUIRE(temporaryRoot.isValid());

    const auto output = QDir(temporaryRoot.path()).filePath("nested/config.json");
    REQUIRE(StringToFile("{\"ok\":true}", output));
    REQUIRE(QFile::exists(output));
    REQUIRE(StringFromFile(output) == "{\"ok\":true}");

    const auto directoryTarget = QDir(temporaryRoot.path()).filePath("directory-target");
    REQUIRE(QDir().mkpath(directoryTarget));
    REQUIRE_FALSE(StringToFile("must fail", directoryTarget));
}

TEST_CASE("Strict JSON object reads distinguish missing invalid and valid metadata")
{
    QTemporaryDir temporaryRoot;
    REQUIRE(temporaryRoot.isValid());

    const auto missing = QDir(temporaryRoot.path()).filePath("missing.json");
    REQUIRE(ReadJsonObjectFile(missing).status == JsonObjectFileStatus::Missing);

    const auto invalid = QDir(temporaryRoot.path()).filePath("invalid.json");
    REQUIRE(StringToFile("{ invalid json", invalid));
    const auto invalidResult = ReadJsonObjectFile(invalid);
    REQUIRE(invalidResult.status == JsonObjectFileStatus::Invalid);
    REQUIRE_FALSE(invalidResult.error.isEmpty());

    const auto arrayRoot = QDir(temporaryRoot.path()).filePath("array.json");
    REQUIRE(StringToFile("[]", arrayRoot));
    REQUIRE(ReadJsonObjectFile(arrayRoot).status == JsonObjectFileStatus::Invalid);

    const auto valid = QDir(temporaryRoot.path()).filePath("valid.json");
    REQUIRE(StringToFile("{\"connection\":{\"name\":\"kept\"}}", valid));
    const auto validResult = ReadJsonObjectFile(valid);
    REQUIRE(validResult.status == JsonObjectFileStatus::Valid);
    REQUIRE(validResult.object.value("connection").toObject().value("name").toString() == "kept");
}

TEST_CASE("Subscription membership preserves unmatched nodes when removal is declined")
{
    const ConnectionId a{ "a" };
    const ConnectionId b{ "b" };
    const ConnectionId c{ "c" };

    const auto delta = BuildSubscriptionMembership({ a, b }, { a, c }, false);
    REQUIRE((delta.finalConnections == QList<ConnectionId>{ a, c, b }));
    REQUIRE((delta.added == QList<ConnectionId>{ c }));
    REQUIRE(delta.removed.isEmpty());
}

TEST_CASE("Subscription membership removes only explicitly confirmed unmatched nodes")
{
    const ConnectionId a{ "a" };
    const ConnectionId b{ "b" };
    const ConnectionId c{ "c" };

    const auto delta = BuildSubscriptionMembership({ a, b }, { a, c }, true);
    REQUIRE((delta.finalConnections == QList<ConnectionId>{ a, c }));
    REQUIRE((delta.added == QList<ConnectionId>{ c }));
    REQUIRE((delta.removed == QList<ConnectionId>{ b }));
}
