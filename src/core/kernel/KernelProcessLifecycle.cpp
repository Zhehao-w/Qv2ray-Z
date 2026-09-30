#include "KernelProcessLifecycle.hpp"

#include <QEventLoop>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QtGlobal>

namespace Qv2ray::core::kernel
{
    QString TakeProcessDiagnostics(QProcess &process)
    {
        QStringList details;
        const auto processError = process.errorString().trimmed();
        if (!processError.isEmpty() && processError.compare(QStringLiteral("Unknown error"), Qt::CaseInsensitive) != 0)
            details << processError;

        const auto standardError = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (!standardError.isEmpty())
            details << standardError;

        return details.join(QStringLiteral(" | "));
    }

    bool StartProcessBounded(QProcess &process, int timeoutMs, QString *error)
    {
        const auto started = process.waitForStarted(timeoutMs);
        if (started && process.state() == QProcess::Running)
            return true;

        bool stopped = process.state() == QProcess::NotRunning;
        if (!stopped)
        {
            process.kill();
            stopped = process.waitForFinished(qMax(timeoutMs, 100)) || process.state() == QProcess::NotRunning;
        }

        if (error)
        {
            auto detail = TakeProcessDiagnostics(process);
            if (detail.isEmpty())
                detail = QStringLiteral("process did not enter the running state");
            if (!stopped)
                detail += QStringLiteral("; failed startup process could not be killed");
            *error = detail;
        }
        return false;
    }

    bool ConfirmProcessStable(QProcess &process, int graceMs, QString *error)
    {
        if (process.state() != QProcess::Running)
        {
            if (error)
            {
                auto detail = TakeProcessDiagnostics(process);
                if (detail.isEmpty())
                    detail = QStringLiteral("process exited before startup was confirmed");
                *error = detail;
            }
            return false;
        }

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
        timer.start(qMax(graceMs, 0));
        loop.exec(QEventLoop::ExcludeUserInputEvents);

        if (process.state() == QProcess::Running)
            return true;

        if (error)
        {
            auto detail = TakeProcessDiagnostics(process);
            const auto exitDetail = process.exitStatus() == QProcess::CrashExit
                                        ? QStringLiteral("process crashed during startup")
                                        : QStringLiteral("process exited during startup with code %1").arg(process.exitCode());
            *error = detail.isEmpty() ? exitDetail : exitDetail + QStringLiteral(": ") + detail;
        }
        return false;
    }

    bool WaitForProcessFinishedBounded(QProcess &process, int timeoutMs, int killTimeoutMs, QString *error)
    {
        if (process.state() == QProcess::NotRunning)
            return true;

        if (process.waitForFinished(timeoutMs))
            return true;

        process.kill();
        const auto killed = process.waitForFinished(killTimeoutMs) || process.state() == QProcess::NotRunning;
        if (error)
        {
            const auto detail = TakeProcessDiagnostics(process);
            *error = killed ? QStringLiteral("process timed out")
                            : QStringLiteral("process timed out and could not be killed");
            if (!detail.isEmpty())
                *error += QStringLiteral(": ") + detail;
        }
        return false;
    }

    ProcessStopResult StopProcessBounded(QProcess &process, int terminateTimeoutMs, int killTimeoutMs, QString *error)
    {
        if (process.state() == QProcess::NotRunning)
            return ProcessStopResult::AlreadyStopped;

        process.terminate();
        if (process.waitForFinished(terminateTimeoutMs) || process.state() == QProcess::NotRunning)
            return ProcessStopResult::Terminated;

        process.kill();
        if (process.waitForFinished(killTimeoutMs) || process.state() == QProcess::NotRunning)
            return ProcessStopResult::Killed;

        if (error)
        {
            auto detail = TakeProcessDiagnostics(process);
            if (detail.isEmpty())
                detail = QStringLiteral("process remained running after terminate and kill");
            *error = detail;
        }
        return ProcessStopResult::Failed;
    }
} // namespace Qv2ray::core::kernel
