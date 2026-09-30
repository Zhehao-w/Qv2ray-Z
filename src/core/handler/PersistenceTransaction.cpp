#include "PersistenceTransaction.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace Qv2ray::core::handler::data_safety
{
    namespace
    {
        constexpr auto JOURNAL_DIRECTORY_NAME = ".persistence-transaction";
        constexpr auto MANIFEST_FILE_NAME = "manifest.json";
        constexpr auto OLD_DIRECTORY_NAME = "old";
        constexpr auto NEW_DIRECTORY_NAME = "new";
        constexpr int MANIFEST_VERSION = 1;

        struct JournalEntry
        {
            QString relativeTarget;
            bool oldExists = false;
            bool newExists = false;
            QByteArray oldHash;
            QByteArray newHash;
        };

        struct JournalManifest
        {
            QString phase;
            QList<JournalEntry> entries;
        };

        struct JournalRetirementResult
        {
            bool authorityRemoved = false;
            bool directoryRemoved = false;
            QString error;
        };

        QByteArray Sha256(const QByteArray &data)
        {
            return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
        }

        Qt::CaseSensitivity PathCaseSensitivity()
        {
#ifdef Q_OS_WIN
            return Qt::CaseInsensitive;
#else
            return Qt::CaseSensitive;
#endif
        }

        QString TargetKey(const QString &relativeTarget)
        {
#ifdef Q_OS_WIN
            return relativeTarget.toCaseFolded();
#else
            return relativeTarget;
#endif
        }

        bool EnsureParentDirectory(const QString &targetPath, QString *error)
        {
            const QFileInfo info(targetPath);
            if (info.dir().exists())
                return true;
            if (info.dir().mkpath(info.dir().path()))
                return true;
            if (error)
                *error = QStringLiteral("Cannot create parent directory for %1").arg(targetPath);
            return false;
        }

        bool WriteBytesAtomically(const QString &targetPath, const QByteArray &data, QString *error)
        {
            if (!EnsureParentDirectory(targetPath, error))
                return false;

            QSaveFile file(targetPath);
            if (!file.open(QIODevice::WriteOnly))
            {
                if (error)
                    *error = QStringLiteral("Cannot open %1 for writing: %2").arg(targetPath, file.errorString());
                return false;
            }

            if (file.write(data) != data.size())
            {
                if (error)
                    *error = QStringLiteral("Incomplete write for %1: %2").arg(targetPath, file.errorString());
                file.cancelWriting();
                return false;
            }

            if (!file.commit())
            {
                if (error)
                    *error = QStringLiteral("Cannot commit %1: %2").arg(targetPath, file.errorString());
                return false;
            }
            return true;
        }

        bool ReadBytes(const QString &path, QByteArray *data, QString *error)
        {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                if (error)
                    *error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
                return false;
            }
            *data = file.readAll();
            return true;
        }

        bool RemoveFileIfPresent(const QString &path, QString *error)
        {
            const QFileInfo info(path);
            if (!info.exists())
                return true;
            if (!info.isFile())
            {
                if (error)
                    *error = QStringLiteral("Refusing to delete non-file transaction target: %1").arg(path);
                return false;
            }
            if (QFile::remove(path))
                return true;
            if (error)
                *error = QStringLiteral("Cannot delete transaction target: %1").arg(path);
            return false;
        }

        bool ResolveTarget(const QString &rootDirectory, const QString &targetPath, QString *relativeTarget, QString *absoluteTarget, QString *error)
        {
            const QDir root(QDir::cleanPath(QFileInfo(rootDirectory).absoluteFilePath()));
            const QString absolute = QDir::cleanPath(QFileInfo(targetPath).isAbsolute() ? QFileInfo(targetPath).absoluteFilePath()
                                                                                       : root.absoluteFilePath(targetPath));
            const QString relative = QDir::cleanPath(root.relativeFilePath(absolute));

            if (relative.isEmpty() || relative == "." || QDir::isAbsolutePath(relative) || relative == ".." || relative.startsWith("../") ||
                relative.startsWith("..\\"))
            {
                if (error)
                    *error = QStringLiteral("Transaction target escapes the configuration root: %1").arg(targetPath);
                return false;
            }

            const auto journalName = QString::fromLatin1(JOURNAL_DIRECTORY_NAME);
            const auto journalPrefix = journalName + "/";
            const auto caseSensitivity = PathCaseSensitivity();
            if (relative.compare(journalName, caseSensitivity) == 0 || relative.startsWith(journalPrefix, caseSensitivity))
            {
                if (error)
                    *error = QStringLiteral("Transaction target overlaps the transaction journal: %1").arg(targetPath);
                return false;
            }

            if (relativeTarget)
                *relativeTarget = relative;
            if (absoluteTarget)
                *absoluteTarget = absolute;
            return true;
        }

        QString PayloadPath(const QString &journalDirectory, bool oldPayload, int index)
        {
            return QDir(journalDirectory).filePath(QStringLiteral("%1/%2.bin").arg(oldPayload ? OLD_DIRECTORY_NAME : NEW_DIRECTORY_NAME).arg(index));
        }

        QJsonObject ManifestToJson(const JournalManifest &manifest)
        {
            QJsonArray entries;
            for (const auto &entry : manifest.entries)
            {
                QJsonObject object;
                object["target"] = entry.relativeTarget;
                object["oldExists"] = entry.oldExists;
                object["newExists"] = entry.newExists;
                if (entry.oldExists)
                    object["oldSha256"] = QString::fromLatin1(entry.oldHash);
                if (entry.newExists)
                    object["newSha256"] = QString::fromLatin1(entry.newHash);
                entries.append(object);
            }

            QJsonObject root;
            root["version"] = MANIFEST_VERSION;
            root["phase"] = manifest.phase;
            root["entries"] = entries;
            return root;
        }

        bool IsSha256String(const QString &value)
        {
            if (value.size() != 64)
                return false;
            for (const auto c : value)
            {
                const bool lowerHex = c >= QLatin1Char('a') && c <= QLatin1Char('f');
                if (!c.isDigit() && !lowerHex)
                    return false;
            }
            return true;
        }

        bool ParseManifest(const QString &rootDirectory, const QByteArray &data, JournalManifest *manifest, QString *error)
        {
            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(data, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
            {
                if (error)
                    *error = QStringLiteral("Invalid persistence transaction manifest: %1").arg(parseError.errorString());
                return false;
            }

            const auto object = document.object();
            if (object.value("version").toInt(-1) != MANIFEST_VERSION)
            {
                if (error)
                    *error = QStringLiteral("Unsupported persistence transaction manifest version.");
                return false;
            }

            const auto phase = object.value("phase").toString();
            if (phase != "prepared" && phase != "committed")
            {
                if (error)
                    *error = QStringLiteral("Invalid persistence transaction phase.");
                return false;
            }

            const auto entriesValue = object.value("entries");
            if (!entriesValue.isArray() || entriesValue.toArray().isEmpty())
            {
                if (error)
                    *error = QStringLiteral("Persistence transaction manifest has no entries.");
                return false;
            }

            JournalManifest parsed;
            parsed.phase = phase;
            QSet<QString> seenTargets;
            for (const auto &value : entriesValue.toArray())
            {
                if (!value.isObject())
                {
                    if (error)
                        *error = QStringLiteral("Persistence transaction manifest contains a non-object entry.");
                    return false;
                }

                const auto entryObject = value.toObject();
                if (!entryObject.value("target").isString() || !entryObject.value("oldExists").isBool() || !entryObject.value("newExists").isBool())
                {
                    if (error)
                        *error = QStringLiteral("Persistence transaction manifest entry is incomplete.");
                    return false;
                }

                QString relativeTarget;
                if (!ResolveTarget(rootDirectory, entryObject.value("target").toString(), &relativeTarget, nullptr, error))
                    return false;
                const auto targetKey = TargetKey(relativeTarget);
                if (seenTargets.contains(targetKey))
                {
                    if (error)
                        *error = QStringLiteral("Persistence transaction manifest contains duplicate target: %1").arg(relativeTarget);
                    return false;
                }
                seenTargets.insert(targetKey);

                JournalEntry entry;
                entry.relativeTarget = relativeTarget;
                entry.oldExists = entryObject.value("oldExists").toBool();
                entry.newExists = entryObject.value("newExists").toBool();

                if (entry.oldExists)
                {
                    const auto hash = entryObject.value("oldSha256").toString();
                    if (!IsSha256String(hash))
                    {
                        if (error)
                            *error = QStringLiteral("Persistence transaction manifest has an invalid old payload hash.");
                        return false;
                    }
                    entry.oldHash = hash.toLatin1();
                }

                if (entry.newExists)
                {
                    const auto hash = entryObject.value("newSha256").toString();
                    if (!IsSha256String(hash))
                    {
                        if (error)
                            *error = QStringLiteral("Persistence transaction manifest has an invalid new payload hash.");
                        return false;
                    }
                    entry.newHash = hash.toLatin1();
                }

                parsed.entries.append(entry);
            }

            *manifest = parsed;
            return true;
        }

        bool ValidateRecoveryPayloads(const QString &journalDirectory, const JournalManifest &manifest, bool restoreOld, QString *error)
        {
            for (int index = 0; index < manifest.entries.size(); ++index)
            {
                const auto &entry = manifest.entries[index];
                const bool payloadExists = restoreOld ? entry.oldExists : entry.newExists;
                if (!payloadExists)
                    continue;

                QByteArray payload;
                const auto payloadPath = PayloadPath(journalDirectory, restoreOld, index);
                if (!ReadBytes(payloadPath, &payload, error))
                    return false;
                const auto expectedHash = restoreOld ? entry.oldHash : entry.newHash;
                if (Sha256(payload) != expectedHash)
                {
                    if (error)
                        *error = QStringLiteral("Persistence transaction payload hash mismatch: %1").arg(payloadPath);
                    return false;
                }
            }
            return true;
        }

        bool ApplyRecoveryState(const QString &rootDirectory, const QString &journalDirectory, const JournalManifest &manifest, bool restoreOld,
                                QString *error)
        {
            if (!ValidateRecoveryPayloads(journalDirectory, manifest, restoreOld, error))
                return false;

            for (int index = 0; index < manifest.entries.size(); ++index)
            {
                const auto &entry = manifest.entries[index];
                QString absoluteTarget;
                if (!ResolveTarget(rootDirectory, entry.relativeTarget, nullptr, &absoluteTarget, error))
                    return false;

                const bool shouldExist = restoreOld ? entry.oldExists : entry.newExists;
                if (!shouldExist)
                {
                    if (!RemoveFileIfPresent(absoluteTarget, error))
                        return false;
                    continue;
                }

                QByteArray payload;
                if (!ReadBytes(PayloadPath(journalDirectory, restoreOld, index), &payload, error))
                    return false;
                if (!WriteBytesAtomically(absoluteTarget, payload, error))
                    return false;
            }
            return true;
        }

        bool RemoveJournalDirectory(const QString &journalDirectory)
        {
            QDir journal(journalDirectory);
            return !journal.exists() || journal.removeRecursively();
        }

        JournalRetirementResult RetireJournal(const QString &journalDirectory)
        {
            const auto manifestPath = QDir(journalDirectory).filePath(MANIFEST_FILE_NAME);
            if (QFile::exists(manifestPath) && !QFile::remove(manifestPath))
            {
                return { false, false, QStringLiteral("Cannot retire the persistence transaction manifest: %1").arg(manifestPath) };
            }

            QDir journal(journalDirectory);
            if (!journal.exists())
                return { true, true, {} };
            if (journal.removeRecursively())
                return { true, true, {} };

            return { true, false, QStringLiteral("Persistence transaction authority was retired, but staged payload cleanup is pending: %1").arg(journalDirectory) };
        }

        QString JoinErrors(const QString &primary, const QString &secondary)
        {
            if (secondary.isEmpty())
                return primary;
            if (primary.isEmpty())
                return secondary;
            return primary + QStringLiteral("; ") + secondary;
        }
    } // namespace

    QString PersistenceTransactionDirectory(const QString &rootDirectory)
    {
        return QDir(QDir::cleanPath(QFileInfo(rootDirectory).absoluteFilePath())).filePath(JOURNAL_DIRECTORY_NAME);
    }

    PersistenceRecoveryResult RecoverPersistenceTransaction(const QString &rootDirectory)
    {
        const auto journalDirectory = PersistenceTransactionDirectory(rootDirectory);
        QDir journal(journalDirectory);
        if (!journal.exists())
            return {};

        const auto manifestPath = journal.filePath(MANIFEST_FILE_NAME);
        if (!QFile::exists(manifestPath))
        {
            if (RemoveJournalDirectory(journalDirectory))
                return {};
            return { PersistenceRecoveryAction::Failed, QStringLiteral("Cannot remove incomplete pre-manifest persistence transaction staging.") };
        }

        QByteArray manifestBytes;
        QString error;
        if (!ReadBytes(manifestPath, &manifestBytes, &error))
            return { PersistenceRecoveryAction::Failed, error };

        JournalManifest manifest;
        if (!ParseManifest(rootDirectory, manifestBytes, &manifest, &error))
            return { PersistenceRecoveryAction::Failed, error };

        const bool restoreOld = manifest.phase == "prepared";
        if (!ApplyRecoveryState(rootDirectory, journalDirectory, manifest, restoreOld, &error))
            return { PersistenceRecoveryAction::Failed, error };

        const auto action = restoreOld ? PersistenceRecoveryAction::RolledBack : PersistenceRecoveryAction::RolledForward;
        const auto retirement = RetireJournal(journalDirectory);
        if (!retirement.authorityRemoved)
            return { PersistenceRecoveryAction::Failed, retirement.error };
        return { action, retirement.directoryRemoved ? QString() : retirement.error };
    }

    PersistenceTransactionResult CommitPersistenceTransaction(const QString &rootDirectory, const QList<PersistenceFileMutation> &mutations)
    {
        if (mutations.isEmpty())
            return { true, true, {} };

        if (!QDir().mkpath(rootDirectory))
            return { false, true, QStringLiteral("Cannot create persistence root directory: %1").arg(rootDirectory) };

        const auto priorRecovery = RecoverPersistenceTransaction(rootDirectory);
        if (!priorRecovery.ok())
            return { false, false, priorRecovery.error };

        const auto journalDirectory = PersistenceTransactionDirectory(rootDirectory);
        if (!QDir().mkpath(QDir(journalDirectory).filePath(OLD_DIRECTORY_NAME)) ||
            !QDir().mkpath(QDir(journalDirectory).filePath(NEW_DIRECTORY_NAME)))
        {
            const bool cleaned = RemoveJournalDirectory(journalDirectory);
            return { false, cleaned, QStringLiteral("Cannot create persistence transaction staging directories.") };
        }

        JournalManifest manifest;
        manifest.phase = "prepared";
        QSet<QString> seenTargets;
        QString error;

        for (int index = 0; index < mutations.size(); ++index)
        {
            const auto &mutation = mutations[index];
            QString relativeTarget;
            QString absoluteTarget;
            if (!ResolveTarget(rootDirectory, mutation.targetPath, &relativeTarget, &absoluteTarget, &error))
            {
                const bool cleaned = RemoveJournalDirectory(journalDirectory);
                return { false, cleaned, error };
            }
            const auto targetKey = TargetKey(relativeTarget);
            if (seenTargets.contains(targetKey))
            {
                const bool cleaned = RemoveJournalDirectory(journalDirectory);
                return { false, cleaned, QStringLiteral("Persistence transaction contains duplicate target: %1").arg(relativeTarget) };
            }
            seenTargets.insert(targetKey);

            const QFileInfo targetInfo(absoluteTarget);
            if (targetInfo.exists() && !targetInfo.isFile())
            {
                const bool cleaned = RemoveJournalDirectory(journalDirectory);
                return { false, cleaned, QStringLiteral("Persistence transaction target is not a regular file: %1").arg(absoluteTarget) };
            }

            JournalEntry entry;
            entry.relativeTarget = relativeTarget;
            entry.oldExists = targetInfo.exists();
            entry.newExists = !mutation.deleteTarget;

            if (entry.oldExists)
            {
                QByteArray oldPayload;
                if (!ReadBytes(absoluteTarget, &oldPayload, &error) ||
                    !WriteBytesAtomically(PayloadPath(journalDirectory, true, index), oldPayload, &error))
                {
                    const bool cleaned = RemoveJournalDirectory(journalDirectory);
                    return { false, cleaned, error };
                }
                entry.oldHash = Sha256(oldPayload);
            }

            if (entry.newExists)
            {
                if (!WriteBytesAtomically(PayloadPath(journalDirectory, false, index), mutation.content, &error))
                {
                    const bool cleaned = RemoveJournalDirectory(journalDirectory);
                    return { false, cleaned, error };
                }
                entry.newHash = Sha256(mutation.content);
            }

            manifest.entries.append(entry);
        }

        if (!ValidateRecoveryPayloads(journalDirectory, manifest, true, &error) || !ValidateRecoveryPayloads(journalDirectory, manifest, false, &error))
        {
            const bool cleaned = RemoveJournalDirectory(journalDirectory);
            return { false, cleaned, error };
        }

        const auto manifestPath = QDir(journalDirectory).filePath(MANIFEST_FILE_NAME);
        if (!WriteBytesAtomically(manifestPath, QJsonDocument(ManifestToJson(manifest)).toJson(QJsonDocument::Compact), &error))
        {
            const auto retirement = RetireJournal(journalDirectory);
            return { false, retirement.authorityRemoved, JoinErrors(error, retirement.error) };
        }

        for (const auto &mutation : mutations)
        {
            QString absoluteTarget;
            if (!ResolveTarget(rootDirectory, mutation.targetPath, nullptr, &absoluteTarget, &error))
                break;

            const bool applied = mutation.deleteTarget ? RemoveFileIfPresent(absoluteTarget, &error)
                                                       : WriteBytesAtomically(absoluteTarget, mutation.content, &error);
            if (!applied)
                break;
        }

        if (!error.isEmpty())
        {
            const auto recovery = RecoverPersistenceTransaction(rootDirectory);
            const auto recoveryDetail = recovery.ok() ? recovery.error : QStringLiteral("rollback failed: ") + recovery.error;
            return { false, recovery.ok(), JoinErrors(error, recoveryDetail) };
        }

        manifest.phase = "committed";
        if (!WriteBytesAtomically(manifestPath, QJsonDocument(ManifestToJson(manifest)).toJson(QJsonDocument::Compact), &error))
        {
            const auto markerError = error;
            const auto recovery = RecoverPersistenceTransaction(rootDirectory);
            const auto recoveryDetail = recovery.ok() ? recovery.error : QStringLiteral("commit-state recovery failed: ") + recovery.error;
            if (recovery.action == PersistenceRecoveryAction::RolledForward)
                return { true, true, JoinErrors(markerError, recoveryDetail) };
            return { false, recovery.ok(), JoinErrors(markerError, recoveryDetail) };
        }

        const auto retirement = RetireJournal(journalDirectory);
        if (!retirement.authorityRemoved || !retirement.directoryRemoved)
            return { true, true, retirement.error };

        return { true, true, {} };
    }
} // namespace Qv2ray::core::handler::data_safety
