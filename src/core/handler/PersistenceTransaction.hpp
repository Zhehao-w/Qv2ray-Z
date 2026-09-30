#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace Qv2ray::core::handler::data_safety
{
    struct PersistenceFileMutation
    {
        QString targetPath;
        QByteArray content;
        bool deleteTarget = false;

        static PersistenceFileMutation Write(const QString &targetPath, const QByteArray &content)
        {
            return { targetPath, content, false };
        }

        static PersistenceFileMutation Delete(const QString &targetPath)
        {
            return { targetPath, {}, true };
        }
    };

    enum class PersistenceRecoveryAction
    {
        Clean,
        RolledBack,
        RolledForward,
        Failed
    };

    struct PersistenceRecoveryResult
    {
        PersistenceRecoveryAction action = PersistenceRecoveryAction::Clean;
        QString error;

        bool ok() const
        {
            return action != PersistenceRecoveryAction::Failed;
        }
    };

    struct PersistenceTransactionResult
    {
        bool committed = false;
        bool recoveryComplete = true;
        QString error;
    };

    QString PersistenceTransactionDirectory(const QString &rootDirectory);
    PersistenceRecoveryResult RecoverPersistenceTransaction(const QString &rootDirectory);
    PersistenceTransactionResult CommitPersistenceTransaction(const QString &rootDirectory, const QList<PersistenceFileMutation> &mutations);
} // namespace Qv2ray::core::handler::data_safety
