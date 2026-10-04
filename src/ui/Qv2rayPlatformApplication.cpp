#include "Qv2rayPlatformApplication.hpp"

#include "components/proxy/QvProxyConfigurator.hpp"
#include "components/proxy/ProxyStateSafety.hpp"
#include "core/settings/SettingsBackend.hpp"
#include "utils/WindowsCommandLine.hpp"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QSessionManager>
#endif

#include <QFile>
#include <QSslSocket>
#define QV_MODULE_NAME "PlatformApplication"

#ifdef QT_DEBUG
const static inline QString QV2RAY_URL_SCHEME = "qv2ray-debug";
#else
const static inline QString QV2RAY_URL_SCHEME = "qv2ray";
#endif

QStringList Qv2rayPlatformApplication::CheckPrerequisites()
{
    QStringList errors;
    if (!QSslSocket::supportsSsl())
    {
        // Subscriptions require TLS support.
        const auto osslReqVersion = QSslSocket::sslLibraryBuildVersionString();
        const auto osslCurVersion = QSslSocket::sslLibraryVersionString();
        LOG("Current OpenSSL version: " + osslCurVersion);
        LOG("Required OpenSSL version: " + osslReqVersion);
        errors << "Qv2ray cannot run without OpenSSL.";
        errors << "This is usually caused by using the wrong version of OpenSSL";
        errors << "Required=" + osslReqVersion + "Current=" + osslCurVersion;
    }
    return errors + checkPrerequisitesInternal();
}

bool Qv2rayPlatformApplication::Initialize()
{
    QString errorMessage;
    bool canContinue;
    const auto hasError = parseCommandLine(&errorMessage, &canContinue);
    if (hasError)
    {
        LOG("Command line:" QVLOG_A(errorMessage));
        if (!canContinue)
        {
            LOG("Fatal, Qv2ray cannot continue.");
            return false;
        }
        else
        {
            LOG("Non-fatal error, continue starting up.");
        }
    }

#ifdef Q_OS_WIN
    const auto appPath = QDir::toNativeSeparators(applicationFilePath());
    const auto regPath = "HKEY_CURRENT_USER\\Software\\Classes\\" + QV2RAY_URL_SCHEME;
    QSettings reg(regPath, QSettings::NativeFormat);
    reg.setValue("Default", "Qv2ray");
    reg.setValue("URL Protocol", "");
    reg.beginGroup("DefaultIcon");
    reg.setValue("Default", QString("%1,1").arg(Qv2ray::utils::windows::QuoteCommandLineArgument(appPath)));
    reg.endGroup();
    reg.beginGroup("shell");
    reg.beginGroup("open");
    reg.beginGroup("command");
    reg.setValue("Default", Qv2ray::utils::windows::BuildUrlProtocolCommand(appPath));
#endif

#ifndef QV2RAY_NO_SINGLEAPPLICATON
    connect(this, &SingleApplication::receivedMessage, this, &Qv2rayPlatformApplication::onMessageReceived, Qt::QueuedConnection);
    if (isSecondary())
    {
        // Older Qv2ray primaries expect these fields in the single-instance
        // message. They are wire compatibility only; maintained Qv2ray-Z does
        // not use them to relaunch an updater or replacement executable.
        StartupArguments.version = QV2RAY_VERSION_STRING;
        StartupArguments.buildVersion = QV2RAY_VERSION_BUILD;
        StartupArguments.fullArgs = arguments();
        if (StartupArguments.arguments.isEmpty())
            StartupArguments.arguments << Qv2rayStartupArguments::NORMAL;
        bool status = sendMessage(JsonToString(StartupArguments.toJson(), QJsonDocument::Compact).toUtf8());
        if (!status)
            LOG("Cannot send message.");
        SetExitReason(EXIT_SECONDARY_INSTANCE);
        return false;
    }
#endif

#ifdef QV2RAY_GUI
#ifdef Q_OS_LINUX
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    setFallbackSessionManagementEnabled(false);
#endif
#endif

#ifdef Q_OS_WIN
    SetCurrentDirectory(applicationDirPath().toStdWString().c_str());
    // Set special font in Windows
    QFont font;
    font.setPointSize(9);
    font.setFamily("Microsoft YaHei");
    setFont(font);
#endif
#endif

    if (!LocateConfiguration())
    {
        LOG("Configuration initialization failed; aborting startup without persisting application state.");
        SetExitReason(EXIT_INITIALIZATION_FAILED);
        return false;
    }

#ifdef Q_OS_WIN
    using namespace Qv2ray::components::proxy::safety;
    SetProxyAccessAllowed(false);
    if (!EnsureProxyProcessLock())
    {
        LOG("Another Qv2ray process owns Windows system-proxy recovery; this process will not recover or modify system proxy state.");
    }
    else
    {
        const auto currentConfigPath = QvCoreApplication->ConfigPath;
        QString previousConfigPath;
        const auto previousStatus = ReadPreviousProxyConfigPath(&previousConfigPath);
        if (previousStatus == ConfigPathRecordStatus::Error)
        {
            LOG("Windows system-proxy recovery location metadata is unreadable; proxy changes are blocked for this process.");
        }
        else
        {
            const auto currentRecoveryPath = ProxyRecoveryRecordPathForConfig(currentConfigPath);
            const auto previousRecoveryPath =
                previousStatus == ConfigPathRecordStatus::Loaded && !ConfigPathsEquivalent(previousConfigPath, currentConfigPath)
                    ? ProxyRecoveryRecordPathForConfig(previousConfigPath)
                    : QString();
            const auto currentRecoveryExists = !currentRecoveryPath.isEmpty() && QFile::exists(currentRecoveryPath);
            const auto previousRecoveryExists = !previousRecoveryPath.isEmpty() && QFile::exists(previousRecoveryPath);

            bool recoverySucceeded = false;
            if (currentRecoveryExists && previousRecoveryExists)
            {
                LOG("Windows system-proxy recovery records exist in both the current and previous configuration locations; refusing ambiguous recovery.");
            }
            else
            {
                if (previousRecoveryExists)
                {
                    LOG("Recovering Windows system proxy from the previous configuration location before using the current profile.");
                    QvCoreApplication->ConfigPath = previousConfigPath;
                    recoverySucceeded = RecoverSystemProxyIfNeeded();
                    QvCoreApplication->ConfigPath = currentConfigPath;
                }
                else
                {
                    recoverySucceeded = RecoverSystemProxyIfNeeded();
                }

                if (!recoverySucceeded)
                {
                    LOG("Windows system proxy recovery remains unresolved; Qv2ray will not overwrite unverified proxy state.");
                }
                else if (!RememberProxyConfigPath(currentConfigPath))
                {
                    LOG("Could not persist the stable Windows proxy recovery location; system proxy changes are blocked for this process.");
                }
                else
                {
                    SetProxyAccessAllowed(true);
                }
            }
        }
    }
#endif

    return true;
}

Qv2rayExitReason Qv2rayPlatformApplication::RunQv2ray()
{
    PluginHost = new QvPluginHost();
    RouteManager = new RouteHandler();
    ConnectionManager = new QvConfigHandler();

#ifdef Q_OS_WIN
    // The durable recovery record is the Windows ownership authority. Release
    // owned proxy state on every real disconnect, regardless of the current
    // automatic-proxy preference, but never bypass the startup fail-closed
    // ownership/access gate. Automatic mode keeps its existing MainWindow
    // notification path; manual mode emits the clear event from this lifecycle
    // hook because no UI cleanup will follow.
    connect(ConnectionManager, &QvConfigHandler::OnDisconnected, this, [](const ConnectionGroupPair &) {
        using namespace Qv2ray::components::proxy::safety;
        if (!CanManageSystemProxy() || !HasProxyRecoveryRecord(QvCoreApplication->ConfigPath))
            return;

        const auto automaticProxy = GlobalConfig.inboundConfig.systemProxySettings.setSystemProxy;
        const auto released = automaticProxy ? RecoverSystemProxyIfNeeded() : ClearSystemProxy();
        if (!released)
            LOG("Windows system proxy ownership could not be released safely after disconnect; recovery state was retained for retry.");
    });
#endif

    // Persistence callbacks are installed only after configuration loading has
    // succeeded and all state managers exist. Initialization failures and
    // secondary instances therefore cannot enter normal shutdown persistence.
    connect(this, &Qv2rayPlatformApplication::aboutToQuit, this, &Qv2rayPlatformApplication::quitInternal);
#ifdef QV2RAY_GUI
#ifdef Q_OS_LINUX
    connect(this, &QGuiApplication::commitDataRequest, [] {
        RouteManager->SaveRoutes();
        ConnectionManager->SaveConnectionConfig();
        PluginHost->SavePluginSettings();
        SaveGlobalSettings();
    });
#endif
#endif

    return runQv2rayInternal();
}

void Qv2rayPlatformApplication::quitInternal()
{
    // Do not change the order.
    ConnectionManager->StopConnection();
#ifdef Q_OS_WIN
    // OnDisconnected normally releases current ownership. Keep a final,
    // idempotent ownership recovery before teardown so exit cannot leave a
    // loopback proxy behind if the normal disconnect signal path was skipped.
    // If startup blocked proxy management, preserve that fail-closed decision.
    if (Qv2ray::components::proxy::safety::CanManageSystemProxy() && !RecoverSystemProxyIfNeeded())
        LOG("Windows system proxy ownership could not be released safely during shutdown; recovery state was retained for the next retry.");
#endif
    RouteManager->SaveRoutes();
    ConnectionManager->SaveConnectionConfig();
    PluginHost->SavePluginSettings();
    SaveGlobalSettings();
    terminateUIInternal();
    delete ConnectionManager;
    delete RouteManager;
    delete PluginHost;
    ConnectionManager = nullptr;
    RouteManager = nullptr;
    PluginHost = nullptr;
}

bool Qv2rayPlatformApplication::parseCommandLine(QString *errorMessage, bool *canContinue)
{
    *canContinue = true;
    QStringList filteredArgs;
    for (const auto &arg : arguments())
    {
#ifdef Q_OS_MACOS
        if (arg.contains("-psn"))
            continue;
#endif
        filteredArgs << arg;
    }
    QCommandLineParser parser;
    //
    QCommandLineOption noAPIOption("noAPI", QObject::tr("Disable gRPC API subsystem"));
    QCommandLineOption noPluginsOption("noPlugin", QObject::tr("Disable plugins feature"));
    QCommandLineOption debugLogOption("debug", QObject::tr("Enable debug output"));
    QCommandLineOption noAutoConnectionOption("noAutoConnection", QObject::tr("Do not automatically connect"));
    QCommandLineOption disconnectOption("disconnect", QObject::tr("Stop current connection"));
    QCommandLineOption reconnectOption("reconnect", QObject::tr("Reconnect last connection"));
    QCommandLineOption exitOption("exit", QObject::tr("Exit Qv2ray"));
    //
    parser.setApplicationDescription(QObject::tr("Qv2ray-Z - A Qt frontend modernized for current Xray-core."));
    parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
    //
    parser.addOption(noAPIOption);
    parser.addOption(noPluginsOption);
    parser.addOption(debugLogOption);
    parser.addOption(noAutoConnectionOption);
    parser.addOption(disconnectOption);
    parser.addOption(reconnectOption);
    parser.addOption(exitOption);
    //
    const auto helpOption = parser.addHelpOption();
    const auto versionOption = parser.addVersionOption();

    if (!parser.parse(filteredArgs))
    {
        *canContinue = true;
        *errorMessage = parser.errorText();
        return false;
    }

    if (parser.isSet(versionOption))
    {
        parser.showVersion();
        return true;
    }

    if (parser.isSet(helpOption))
    {
        parser.showHelp();
        return true;
    }

    for (const auto &arg : parser.positionalArguments())
    {
        if (arg.startsWith(QV2RAY_URL_SCHEME + "://"))
        {
            StartupArguments.arguments << Qv2rayStartupArguments::QV2RAY_LINK;
            StartupArguments.links << arg;
        }
    }

    if (parser.isSet(exitOption))
    {
        DEBUG("disconnectOption is set.");
        StartupArguments.arguments << Qv2rayStartupArguments::EXIT;
    }

    if (parser.isSet(disconnectOption))
    {
        DEBUG("disconnectOption is set.");
        StartupArguments.arguments << Qv2rayStartupArguments::DISCONNECT;
    }

    if (parser.isSet(reconnectOption))
    {
        DEBUG("reconnectOption is set.");
        StartupArguments.arguments << Qv2rayStartupArguments::RECONNECT;
    }

#define ProcessExtraStartupOptions(option)                                                                                                           \
    DEBUG("Startup Options:" QVLOG_A(parser.isSet(option##Option)));                                                                                 \
    StartupArguments.option = parser.isSet(option##Option);

    ProcessExtraStartupOptions(noAPI);
    ProcessExtraStartupOptions(debugLog);
    ProcessExtraStartupOptions(noAutoConnection);
    ProcessExtraStartupOptions(noPlugins);
    return true;
}
