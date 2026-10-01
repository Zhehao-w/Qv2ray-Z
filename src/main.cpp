#include <QtGlobal>

#ifdef QV2RAY_CLI
#include "ui/cli/Qv2rayCliApplication.hpp"
#endif

#ifdef QV2RAY_GUI_QWIDGETS
#include "ui/widgets/Qv2rayWidgetApplication.hpp"
#endif

#ifdef QV2RAY_GUI_QML
#include "ui/qml/Qv2rayQMLApplication.hpp"
#endif

#include "utils/DiagnosticSafety.hpp"
#include "utils/QvHelpers.hpp"

#ifndef Q_OS_WIN
#include <QSocketNotifier>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
#endif

#define QV_MODULE_NAME "Init"

int globalArgc;
char **globalArgv;

#ifndef Q_OS_WIN
namespace
{
    constexpr unsigned int PendingTerminate = 1u << 0;
    constexpr unsigned int PendingHangup = 1u << 1;
    constexpr unsigned int PendingRestart = 1u << 2;
    constexpr unsigned int PendingStop = 1u << 3;
    constexpr unsigned int PendingShutdown = PendingTerminate | PendingHangup;

    int controlSignalPipe[2] = { -1, -1 };
    volatile sig_atomic_t controlSignalWriteFd = -1;
    std::atomic<unsigned int> pendingControlSignals{ 0 };
    static_assert(std::atomic<unsigned int>::is_always_lock_free, "POSIX signal pending state must use lock-free atomics.");

    unsigned int controlSignalBit(int signum) noexcept
    {
        switch (signum)
        {
            case SIGTERM: return PendingTerminate;
            case SIGHUP: return PendingHangup;
            case SIGUSR1: return PendingRestart;
            case SIGUSR2: return PendingStop;
            default: return 0;
        }
    }

    void controlSignalHandler(int signum) noexcept
    {
        const int savedErrno = errno;
        const auto pendingBit = controlSignalBit(signum);
        if (pendingBit != 0)
            pendingControlSignals.fetch_or(pendingBit, std::memory_order_relaxed);

        const auto writeFd = controlSignalWriteFd;
        if (writeFd >= 0)
        {
            // The pipe is wakeup-only. If it is full, the lock-free pending mask
            // still retains the control action until the Qt thread drains it.
            constexpr unsigned char wakeByte = 1;
            const auto ignored = ::write(static_cast<int>(writeFd), &wakeByte, sizeof(wakeByte));
            Q_UNUSED(ignored)
        }
        errno = savedErrno;
    }

    bool setNonBlocking(int fd)
    {
        const auto flags = ::fcntl(fd, F_GETFL, 0);
        return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool setCloseOnExec(int fd)
    {
        const auto flags = ::fcntl(fd, F_GETFD, 0);
        return flags >= 0 && ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
    }

    bool installSignalAction(int signum, void (*handler)(int))
    {
        struct sigaction action
        {
        };
        action.sa_handler = handler;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        return ::sigaction(signum, &action, nullptr) == 0;
    }

    void dispatchPendingControlSignals()
    {
        const auto pending = pendingControlSignals.exchange(0, std::memory_order_acq_rel);
        if (pending == 0)
            return;

        // Shutdown requests take precedence over connection-management actions.
        if ((pending & PendingShutdown) != 0)
        {
            QCoreApplication::quit();
            return;
        }

        if ((pending & PendingRestart) != 0 && ConnectionManager)
            ConnectionManager->RestartConnection();
        if ((pending & PendingStop) != 0 && ConnectionManager)
            ConnectionManager->StopConnection();
    }

    bool installControlSignalBridge(QObject *context)
    {
        if (::pipe(controlSignalPipe) != 0)
            return false;
        if (!setNonBlocking(controlSignalPipe[0]) || !setNonBlocking(controlSignalPipe[1]) || !setCloseOnExec(controlSignalPipe[0]) ||
            !setCloseOnExec(controlSignalPipe[1]))
        {
            ::close(controlSignalPipe[0]);
            ::close(controlSignalPipe[1]);
            controlSignalPipe[0] = -1;
            controlSignalPipe[1] = -1;
            controlSignalWriteFd = -1;
            return false;
        }

        auto notifier = new QSocketNotifier(controlSignalPipe[0], QSocketNotifier::Read, context);
        QObject::connect(notifier, &QSocketNotifier::activated, context, [notifier]() {
            notifier->setEnabled(false);
            unsigned char wakeBytes[64];
            while (::read(controlSignalPipe[0], wakeBytes, sizeof(wakeBytes)) > 0)
            {
            }
            dispatchPendingControlSignals();
            notifier->setEnabled(true);
        });

        controlSignalWriteFd = static_cast<sig_atomic_t>(controlSignalPipe[1]);
        bool success = true;
        for (const auto signum : Qv2ray::common::diagnostics::ControlSignals())
            success = installSignalAction(signum, controlSignalHandler) && success;
        return success;
    }
} // namespace
#endif

void BootstrapMessageBox(const QString &title, const QString &text)
{
#ifdef QV2RAY_GUI
    if (qApp)
    {
        QMessageBox::warning(nullptr, title, text);
    }
    else
    {
        QApplication p(globalArgc, globalArgv);
        QMessageBox::warning(nullptr, title, text);
    }
#else
    std::cout << title.toStdString() << NEWLINE << text.toStdString() << std::endl;
#endif
}

const QString SayLastWords() noexcept
{
    int activeKernelCount = 0;
    int pluginCount = 0;
    if (KernelInstance)
        activeKernelCount = KernelInstance->GetActiveKernelProtocols().count();
    if (PluginHost)
        pluginCount = PluginHost->AllPlugins().count();

    const auto config = QvCoreApplication ? &GlobalConfig : nullptr;
    return Qv2ray::common::diagnostics::BuildSafeDiagnosticReport(config, activeKernelCount, pluginCount);
}

int main(int argc, char *argv[])
{
    globalArgc = argc;
    globalArgv = argv;

    // Fatal crashes deliberately retain the platform's native handling so core
    // dumps / Windows Error Reporting are not replaced by application code.

    // This line must be called before any other ones, since we are using these
    // values to identify instances.
    QCoreApplication::setApplicationVersion(QV2RAY_VERSION_STRING);

#ifdef QT_DEBUG
    QCoreApplication::setApplicationName("qv2ray_debug");
#else
    QCoreApplication::setApplicationName("qv2ray");
#endif

#ifdef QV2RAY_GUI
    QApplication::setApplicationDisplayName("Qv2ray-Z");
#endif

#ifdef QT_DEBUG
    std::cerr << "WARNING: ================ This is a debug build, many features are not stable enough. ================" << std::endl;
#endif

    if (qEnvironmentVariableIsSet("QV2RAY_NO_SCALE_FACTORS"))
    {
        LOG("Force set QT_SCALE_FACTOR to 1.");
        DEBUG("UI", "Original QT_SCALE_FACTOR was:", qEnvironmentVariable("QT_SCALE_FACTOR"));
        qputenv("QT_SCALE_FACTOR", "1");
    }
    else
    {
        DEBUG("High DPI scaling is enabled.");
#ifndef QV2RAY_QT6
        QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#ifdef QV2RAY_GUI
        QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
#endif
    }

#ifndef QV2RAY_QT6
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
#endif

    Qv2rayApplication app(argc, argv);
#ifndef Q_OS_WIN
    if (!installControlSignalBridge(&app))
        LOG("Failed to install the POSIX control-signal bridge.");
#endif

    if (const auto list = app.CheckPrerequisites(); !list.isEmpty())
    {
        BootstrapMessageBox("Qv2ray Prerequisites Check Failed", list.join(NEWLINE));
        return Qv2rayExitReason::EXIT_PRECONDITION_FAILED;
    }

    if (!app.Initialize())
    {
        const auto reason = app.GetExitReason();
        if (reason == EXIT_INITIALIZATION_FAILED)
        {
            BootstrapMessageBox("Qv2ray Initialization Failed", "PreInitialization Failed." NEWLINE "For more information, please see the log.");
            LOG("Qv2ray initialization failed:", reason);
        }
        return reason;
    }

    app.RunQv2ray();
    const auto reason = app.GetExitReason();
    if (reason == EXIT_NEW_VERSION_TRIGGER)
    {
        LOG("Starting new version of Qv2ray: " + app.StartupArguments._qvNewVersionPath);
        QProcess::startDetached(app.StartupArguments._qvNewVersionPath, {});
    }
    return reason;
}
