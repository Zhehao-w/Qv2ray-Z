#pragma once

#include <QString>

class QProcess;

namespace Qv2ray::core::kernel
{
    enum class ProcessStopResult
    {
        AlreadyStopped,
        Terminated,
        Killed,
        Failed,
    };

    bool StartProcessBounded(QProcess &process, int timeoutMs, QString *error = nullptr);
    bool WaitForProcessFinishedBounded(QProcess &process, int timeoutMs, int killTimeoutMs, QString *error = nullptr);
    ProcessStopResult StopProcessBounded(QProcess &process, int terminateTimeoutMs, int killTimeoutMs, QString *error = nullptr);
    QString TakeProcessDiagnostics(QProcess &process);
} // namespace Qv2ray::core::kernel
