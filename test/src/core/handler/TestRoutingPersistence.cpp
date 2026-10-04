#include "core/handler/RouteStorage.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>

#include "catch.hpp"

using namespace Qv2ray::core::handler::route_storage;

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
