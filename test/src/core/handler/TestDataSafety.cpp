#include "core/handler/ConfigDataSafety.hpp"
#include "core/handler/PersistenceTransaction.hpp"
#include "utils/QvHelpers.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <optional>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

using Qv2ray::common::JsonObjectFileStatus;
using namespace Qv2ray::core::handler::data_safety;

namespace
{
    QByteArray sha256(const QByteArray &data)
    {
        return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    }

    void writeBytes(const QString &path, const QByteArray &data)
    {
        REQUIRE(QDir().mkpath(QFileInfo(path).dir().path()));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(data) == data.size());
    }

    void stageJournal(const QString &root, const QString &phase, const QList<QString> &targets, const QList<std::optional<QByteArray>> &oldPayloads,
                      const QList<std::optional<QByteArray>> &newPayloads)
    {
        REQUIRE(targets.size() == oldPayloads.size());
        REQUIRE(targets.size() == newPayloads.size());

        const auto journal = PersistenceTransactionDirectory(root);
        QJsonArray entries;
        for (int i = 0; i < targets.size(); ++i)
        {
            QJsonObject entry;
            entry["target"] = targets[i];
            entry["oldExists"] = oldPayloads[i].has_value();
            entry["newExists"] = newPayloads[i].has_value();
            if (oldPayloads[i].has_value())
            {
                const auto payload = *oldPayloads[i];
                writeBytes(QDir(journal).filePath(QStringLiteral("old/%1.bin").arg(i)), payload);
                entry["oldSha256"] = QString::fromLatin1(sha256(payload));
            }
            if (newPayloads[i].has_value())
            {
                const auto payload = *newPayloads[i];
                writeBytes(QDir(journal).filePath(QStringLiteral("new/%1.bin").arg(i)), payload);
                entry["newSha256"] = QString::fromLatin1(sha256(payload));
            }
            entries.append(entry);
        }

        QJsonObject manifest;
        manifest["version"] = 1;
        manifest["phase"] = phase;
        manifest["entries"] = entries;
        writeBytes(QDir(journal).filePath("manifest.json"), QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    }
} // namespace

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

TEST_CASE("Persistence transaction commits writes and deletes as one unit")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());

    const auto connections = QDir(root.path()).filePath("connections.json");
    const auto groups = QDir(root.path()).filePath("groups.json");
    const auto connectionRoot = QDir(root.path()).filePath("connections/a.json");
    const auto obsoleteRoot = QDir(root.path()).filePath("connections/obsolete.json");
    writeBytes(connections, "old-connections");
    writeBytes(groups, "old-groups");
    writeBytes(connectionRoot, "old-root");
    writeBytes(obsoleteRoot, "obsolete-root");

    const auto result = CommitPersistenceTransaction(
        root.path(), { PersistenceFileMutation::Write(connectionRoot, "new-root"), PersistenceFileMutation::Write(connections, "new-connections"),
                       PersistenceFileMutation::Write(groups, "new-groups"), PersistenceFileMutation::Delete(obsoleteRoot) });

    REQUIRE(result.committed);
    REQUIRE(result.recoveryComplete);
    REQUIRE(StringFromFile(connections) == "new-connections");
    REQUIRE(StringFromFile(groups) == "new-groups");
    REQUIRE(StringFromFile(connectionRoot) == "new-root");
    REQUIRE_FALSE(QFile::exists(obsoleteRoot));
    REQUIRE_FALSE(QDir(PersistenceTransactionDirectory(root.path())).exists());
}

TEST_CASE("Prepared persistence journal rolls back a partially applied multi-file transaction")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());

    const QStringList targets{ "connections/a.json", "connections.json", "groups.json", "connections/removed.json" };
    const QList<std::optional<QByteArray>> oldPayloads{ QByteArray("old-root"), QByteArray("old-connections"), QByteArray("old-groups"),
                                                       QByteArray("old-removed") };
    const QList<std::optional<QByteArray>> newPayloads{ QByteArray("new-root"), QByteArray("new-connections"), QByteArray("new-groups"), std::nullopt };
    stageJournal(root.path(), "prepared", targets, oldPayloads, newPayloads);

    writeBytes(QDir(root.path()).filePath(targets[0]), "new-root");
    writeBytes(QDir(root.path()).filePath(targets[1]), "new-connections");
    REQUIRE_FALSE(QFile::exists(QDir(root.path()).filePath(targets[2])));
    REQUIRE_FALSE(QFile::exists(QDir(root.path()).filePath(targets[3])));

    const auto recovery = RecoverPersistenceTransaction(root.path());
    REQUIRE(recovery.action == PersistenceRecoveryAction::RolledBack);
    for (int i = 0; i < targets.size(); ++i)
        REQUIRE(StringFromFile(QDir(root.path()).filePath(targets[i])).toUtf8() == *oldPayloads[i]);
    REQUIRE_FALSE(QDir(PersistenceTransactionDirectory(root.path())).exists());
}

TEST_CASE("Committed persistence journal rolls forward all targets after a crash before cleanup")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());

    const QStringList targets{ "connections/a.json", "connections.json", "groups.json", "connections/removed.json" };
    const QList<std::optional<QByteArray>> oldPayloads{ QByteArray("old-root"), QByteArray("old-connections"), QByteArray("old-groups"),
                                                       QByteArray("old-removed") };
    const QList<std::optional<QByteArray>> newPayloads{ QByteArray("new-root"), QByteArray("new-connections"), QByteArray("new-groups"), std::nullopt };
    stageJournal(root.path(), "committed", targets, oldPayloads, newPayloads);

    writeBytes(QDir(root.path()).filePath(targets[0]), "new-root");
    writeBytes(QDir(root.path()).filePath(targets[1]), "old-connections");
    writeBytes(QDir(root.path()).filePath(targets[2]), "old-groups");
    writeBytes(QDir(root.path()).filePath(targets[3]), "old-removed");

    const auto recovery = RecoverPersistenceTransaction(root.path());
    REQUIRE(recovery.action == PersistenceRecoveryAction::RolledForward);
    REQUIRE(StringFromFile(QDir(root.path()).filePath(targets[0])) == "new-root");
    REQUIRE(StringFromFile(QDir(root.path()).filePath(targets[1])) == "new-connections");
    REQUIRE(StringFromFile(QDir(root.path()).filePath(targets[2])) == "new-groups");
    REQUIRE_FALSE(QFile::exists(QDir(root.path()).filePath(targets[3])));
    REQUIRE_FALSE(QDir(PersistenceTransactionDirectory(root.path())).exists());
}

TEST_CASE("Recovery fails closed before applying a corrupt staged payload")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());

    const QStringList targets{ "connections.json", "groups.json" };
    const QList<std::optional<QByteArray>> oldPayloads{ QByteArray("old-connections"), QByteArray("old-groups") };
    const QList<std::optional<QByteArray>> newPayloads{ QByteArray("new-connections"), QByteArray("new-groups") };
    stageJournal(root.path(), "committed", targets, oldPayloads, newPayloads);
    writeBytes(QDir(root.path()).filePath(targets[0]), "mixed-connections");
    writeBytes(QDir(root.path()).filePath(targets[1]), "mixed-groups");
    writeBytes(QDir(PersistenceTransactionDirectory(root.path())).filePath("new/1.bin"), "corrupt");

    const auto recovery = RecoverPersistenceTransaction(root.path());
    REQUIRE(recovery.action == PersistenceRecoveryAction::Failed);
    REQUIRE(StringFromFile(QDir(root.path()).filePath(targets[0])) == "mixed-connections");
    REQUIRE(StringFromFile(QDir(root.path()).filePath(targets[1])) == "mixed-groups");
    REQUIRE(QDir(PersistenceTransactionDirectory(root.path())).exists());
}

TEST_CASE("Recovery rejects journal targets outside the configuration root")
{
    QTemporaryDir parent;
    REQUIRE(parent.isValid());
    const auto root = QDir(parent.path()).filePath("config");
    REQUIRE(QDir().mkpath(root));
    const auto outside = QDir(parent.path()).filePath("outside.json");
    writeBytes(outside, "untouched");

    stageJournal(root, "committed", { "../outside.json" }, { QByteArray("untouched") }, { QByteArray("attacker") });
    const auto recovery = RecoverPersistenceTransaction(root);

    REQUIRE(recovery.action == PersistenceRecoveryAction::Failed);
    REQUIRE(StringFromFile(outside) == "untouched");
}

TEST_CASE("Pre-manifest staging is discarded without touching user files")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const auto target = QDir(root.path()).filePath("connections.json");
    writeBytes(target, "authoritative");
    writeBytes(QDir(PersistenceTransactionDirectory(root.path())).filePath("new/0.bin"), "staged-only");

    const auto recovery = RecoverPersistenceTransaction(root.path());
    REQUIRE(recovery.action == PersistenceRecoveryAction::Clean);
    REQUIRE(StringFromFile(target) == "authoritative");
    REQUIRE_FALSE(QDir(PersistenceTransactionDirectory(root.path())).exists());
}
