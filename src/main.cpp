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
#include <fcntl.h>
#include <unistd.h>
#endif

#define QV_MODULE_NAME "Init"

int globalArgc;
char **globalArgv;

#ifndef Q_OS_WIN
namespace
{
    int controlSignalPipe[2] = { -1, -1 };

    void controlSignalHandler(int signum) noexcept
    {
        if (controlSignalPipe[1] < 0)
            return;
        const unsigned char signalByte = static_cast<unsigned char>(signum);
        const auto ignored = ::write(controlSignalPipe[1], &signalByte, sizeof(signalByte));
        Q_UNUSED(ignored)
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

    void dispatchControlSignal(int signum)
    {
        switch (signum)
        {
            case SIGTERM:
            case SIGHUP: QCoreApplication::quit(); break;
            case SIGUSR1:
                if (ConnectionManager)
                    ConnectionManager->RestartConnection();
                break;
            case SIGUSR2:
                if (ConnectionManager)
                    ConnectionManager->StopConnection();
                break;
            default: break;
        }
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
            return false;
        }

        auto notifier = new QSocketNotifier(controlSignalPipe[0], QSocketNotifier::Read, context);
        QObject::connect(notifier, &QSocketNotifier::activated, context, [notifier]() {
            notifier->setEnabled(false);
            unsigned char pendingSignals[64];
            while (true)
            {
                const auto count = ::read(controlSignalPipe[0], pendingSignals, sizeof(pendingSignals));
                if (count <= 0)
                    break;
                for (decltype(count) i = 0; i < count; ++i)
                    dispatchControlSignal(static_cast<int>(pendingSignals[i]));
            }
            notifier->setEnabled(true);
        });

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
