#include "V2RayKernelInteractions.hpp"

#include "APIBackend.hpp"
#include "KernelProcessLifecycle.hpp"
#include "core/connection/ConnectionIO.hpp"
#include "utils/QvHelpers.hpp"

#include <QCoreApplication>
#include <QProcess>

#define QV2RAY_GENERATED_FILE_PATH (QV2RAY_GENERATED_DIR + "config.gen.json")
#define QV_MODULE_NAME "V2RayInteraction"

#ifdef QV2RAY_USE_V5_CORE
#define V2RAY_CORE_VERSION_ARGV "version"
#define V2RAY_CORE_CONFIG_ARGV "run", "-config"
#else
#define V2RAY_CORE_VERSION_ARGV "--version"
#define V2RAY_CORE_CONFIG_ARGV "--config"
#endif

namespace
{
    constexpr int PROCESS_START_TIMEOUT_MS = 5000;
    constexpr int PROCESS_STABILITY_GRACE_MS = 200;
    constexpr int VERSION_FINISH_TIMEOUT_MS = 5000;
    constexpr int CONFIG_VALIDATION_TIMEOUT_MS = 10000;
    constexpr int PROCESS_TERMINATE_TIMEOUT_MS = 1500;
    constexpr int PROCESS_KILL_TIMEOUT_MS = 2000;
} // namespace

namespace Qv2ray::core::kernel
{
    KernelPaths V2RayKernelInstance::EffectiveKernelPaths(const QString &configuredExecutable, const QString &configuredAssets)
    {
#ifdef Q_OS_WIN
        return ResolveBundledKernelPaths(configuredExecutable, configuredAssets, QCoreApplication::applicationDirPath(), "xray.exe");
#else
        return { configuredExecutable, configuredAssets, false };
#endif
    }

#if QV2RAY_FEATURE(kernel_check_permission)
    std::pair<bool, std::optional<QString>> V2RayKernelInstance::CheckAndSetCoreExecutableState(const QString &vCorePath)
    {
#ifdef Q_OS_UNIX
        // For Linux/macOS users: if they cannot execute the core,
        // then we shall grant the permission to execute it.
        QFile coreFile(vCorePath);
        if (!coreFile.permissions().testFlag(QFileDevice::ExeUser))
        {
#if QV2RAY_FEATURE(kernel_set_permission)
            DEBUG("Core file not executable. Trying to enable.");
            const auto result = coreFile.setPermissions(coreFile.permissions().setFlag(QFileDevice::ExeUser));
            if (!result)
            {
                DEBUG("Failed to enable executable permission.");
                const auto message = tr("Core file is lacking executable permission for the current user.") +
                                     tr("Qv2ray tried to set, but failed because permission denied.");
                return { false, message };
            }
            else
            {
                DEBUG("Core executable permission set.");
            }
#endif
            LOG("Core file not executable.");
            return { false, tr("Core file not executable.") };
        }
        else
        {
            DEBUG("Core file is executable.");
        }
        return { true, std::nullopt };
#else
        // For Windows and other users: just skip this check.
        DEBUG("Skipped check and set core executable state.");
        return { true, tr("Check is skipped") };
#endif
    }
#endif

    std::pair<bool, std::optional<QString>> V2RayKernelInstance::ValidateKernel(const QString &corePath, const QString &assetsPath)
    {
        QFile coreFile(corePath);

        if (!coreFile.exists())
            return { false, tr("V2Ray core executable not found.") };

        // Use open() here to prevent `executing` a folder, which may have the
        // same name as the V2Ray core.
        if (!coreFile.open(QFile::ReadOnly))
            return { false, tr("V2Ray core file cannot be opened, please ensure there's a file instead of a folder.") };

        coreFile.close();

#if QV2RAY_FEATURE(kernel_check_abi)
        // Get Core ABI.
        const auto [abi, err] = kernel::abi::deduceKernelABI(corePath);
        if (err)
        {
            LOG("Core ABI deduction failed: " + *err);
            return { false, *err };
        }
        LOG("Core ABI: " + kernel::abi::abiToString(*abi));

        // Get Compiled ABI
        auto compiledABI = kernel::abi::COMPILED_ABI_TYPE;
        LOG("Host ABI: " + kernel::abi::abiToString(compiledABI));

        // Check ABI Compatibility.
        switch (kernel::abi::checkCompatibility(compiledABI, *abi))
        {
            case kernel::abi::ABI_NOPE:
            {
                LOG("Host is incompatible with core");
                const auto msg = tr("V2Ray core is incompatible with your platform.\r\n"
                                    "Expected core ABI is %1, but got actual %2.\r\n"
                                    "Maybe you have downloaded the wrong core?")
                                     .arg(kernel::abi::abiToString(compiledABI), kernel::abi::abiToString(*abi));
                return { false, msg };
            }
            case kernel::abi::ABI_MAYBE:
            {
                LOG("WARNING: Host maybe incompatible with core");
                break;
            }
            case kernel::abi::ABI_PERFECT:
            {
                LOG("Host is compatible with core");
                break;
            }
        }
#endif

#if QV2RAY_FEATURE(kernel_check_permission)
        // Check executable permissions.
        const auto [isExecutableOk, strExecutableErr] = CheckAndSetCoreExecutableState(corePath);
        if (!isExecutableOk)
            return { false, strExecutableErr.value_or("") };
#endif
        //
        // Check file existance.
        // From: https://www.v2fly.org/chapter_02/env.html#asset-location
        bool hasGeoIP = FileExistsIn(QDir(assetsPath), "geoip.dat");
        bool hasGeoSite = FileExistsIn(QDir(assetsPath), "geosite.dat");

        if (!hasGeoIP && !hasGeoSite)
            return { false, tr("V2Ray assets path is not valid.") };

        if (!hasGeoIP)
            return { false, tr("No geoip.dat in assets path.") };

        if (!hasGeoSite)
            return { false, tr("No geosite.dat in assets path.") };

        // Check if V2Ray core returns a version number correctly.
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN32
        // nativeArguments are required for Windows platform, without a
        // reason...
        proc.setProgram(corePath);
        proc.setNativeArguments(V2RAY_CORE_VERSION_ARGV);
        proc.start();
#else
        proc.start(corePath, { V2RAY_CORE_VERSION_ARGV });
#endif

        QString startError;
        if (!StartProcessBounded(proc, PROCESS_START_TIMEOUT_MS, &startError))
            return { false, tr("Failed to start V2Ray core version check: %1").arg(startError) };

        QString finishError;
        if (!WaitForProcessFinishedBounded(proc, VERSION_FINISH_TIMEOUT_MS, PROCESS_KILL_TIMEOUT_MS, &finishError))
            return { false, tr("V2Ray core version check timed out: %1").arg(finishError) };

        const auto output = proc.readAll();
        const auto outputText = QString::fromLocal8Bit(output).trimmed();

        if (proc.exitStatus() == QProcess::CrashExit)
        {
            return { false, outputText.isEmpty() ? tr("V2Ray core version check process crashed.")
                                                 : tr("V2Ray core version check process crashed: %1").arg(outputText) };
        }

        const auto exitCode = proc.exitCode();
        if (exitCode != 0)
        {
            return { false, outputText.isEmpty() ? tr("V2Ray core failed with an exit code: ") + QSTRN(exitCode)
                                                 : tr("V2Ray core failed with exit code %1: %2").arg(exitCode).arg(outputText) };
        }

        LOG("V2Ray output: " + SplitLines(output).join(";"));

        if (SplitLines(output).isEmpty())
            return { false, tr("V2Ray core returns empty string.") };

        return { true, SplitLines(output).at(0) };
    }

    std::optional<QString> V2RayKernelInstance::ValidateConfig(const QString &path)
    {
        const auto paths = EffectiveKernelPaths(GlobalConfig.kernelConfig.KernelPath(), GlobalConfig.kernelConfig.AssetsPath());
        const auto &kernelPath = paths.executable;
        const auto &assetsPath = paths.assets;
        if (const auto &[result, msg] = ValidateKernel(kernelPath, assetsPath); result)
        {
            DEBUG("V2Ray version: " + *msg);
            // Append assets location env.
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("v2ray.location.asset", assetsPath);
            env.insert("XRAY_LOCATION_ASSET", assetsPath);
            //
            QProcess process;
            process.setProcessEnvironment(env);
            process.setProcessChannelMode(QProcess::MergedChannels);
            DEBUG("Starting V2Ray core with test options");
            process.start(kernelPath, { "run", "-test", "-c", path }, QIODevice::ReadWrite | QIODevice::Text);

            QString startError;
            if (!StartProcessBounded(process, PROCESS_START_TIMEOUT_MS, &startError))
                return tr("Failed to start Xray configuration validation: %1").arg(startError);

            QString finishError;
            if (!WaitForProcessFinishedBounded(process, CONFIG_VALIDATION_TIMEOUT_MS, PROCESS_KILL_TIMEOUT_MS, &finishError))
            {
                const auto output = QString::fromLocal8Bit(process.readAll()).trimmed();
                return output.isEmpty() ? tr("Xray configuration validation timed out: %1").arg(finishError) : output;
            }

            if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            {
                const auto output = QString::fromLocal8Bit(process.readAll()).trimmed();
                if (!output.isEmpty())
                    return output;

                if (process.exitStatus() == QProcess::CrashExit)
                    return tr("Xray configuration validation process crashed.");

                return tr("Xray configuration validation failed with exit code %1.").arg(process.exitCode());
            }

            DEBUG("Config file check passed.");
            return std::nullopt;
        }
        else
        {
            return msg;
        }
    }

    V2RayKernelInstance::V2RayKernelInstance(QObject *parent) : QObject(parent)
    {
        vProcess = new QProcess();
        connect(vProcess, &QProcess::readyReadStandardOutput, this,
                [this]() { emit OnProcessOutputReadyRead(vProcess->readAllStandardOutput().trimmed()); });
        connect(vProcess, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
            DEBUG("V2Ray kernel process status changed: " + QVariant::fromValue(state).toString());

            // If V2Ray exits AFTER we successfully started it and this was not
            // an intentional StopConnection transition, treat it as an error.
            if (kernelStarted && state == QProcess::NotRunning)
            {
                QString message;
                if (vProcess->exitStatus() == QProcess::CrashExit)
                    message = tr("V2Ray kernel process crashed.");
                else
                    message = tr("V2Ray kernel process exited unexpectedly with code %1.").arg(vProcess->exitCode());

                const auto diagnostics = TakeProcessDiagnostics(*vProcess);
                if (!diagnostics.isEmpty())
                    message += QStringLiteral(" ") + diagnostics;

                LOG(message);
                StopConnection();
                emit OnProcessErrored(message);
            }
        });
        connect(vProcess, &QProcess::errorOccurred, this,
                [](QProcess::ProcessError error) { DEBUG("V2Ray kernel process error: " + QSTRN(static_cast<int>(error))); });
        apiWorker = new APIWorker();
        qRegisterMetaType<StatisticsType>();
        qRegisterMetaType<QMap<StatisticsType, QvStatsSpeed>>();
        connect(apiWorker, &APIWorker::onAPIDataReady, this, &V2RayKernelInstance::OnNewStatsDataArrived);
    }

    std::optional<QString> V2RayKernelInstance::StartConnection(const CONFIGROOT &root)
    {
        if (kernelStarted)
        {
            LOG("Status is invalid, expect STOPPED when calling StartConnection");
            return tr("Invalid V2Ray Instance Status.");
        }

        apiEnabled = false;
        const auto json = JsonToString(root);
        if (!StringToFile(json, QV2RAY_GENERATED_FILE_PATH))
        {
            return tr("Failed to write the generated Xray configuration. The previous generated config will not be reused.");
        }
        //
        auto filePath = QV2RAY_GENERATED_FILE_PATH;

        if (const auto &result = ValidateConfig(filePath); result)
        {
            kernelStarted = false;
            return tr("V2Ray kernel failed to start: ") + *result;
        }
        const auto paths = EffectiveKernelPaths(GlobalConfig.kernelConfig.KernelPath(), GlobalConfig.kernelConfig.AssetsPath());
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("v2ray.location.asset", paths.assets);
        env.insert("XRAY_LOCATION_ASSET", paths.assets);
        vProcess->setProcessEnvironment(env);
        vProcess->start(paths.executable, { V2RAY_CORE_CONFIG_ARGV, filePath }, QIODevice::ReadWrite | QIODevice::Text);

        QString startError;
        if (!StartProcessBounded(*vProcess, PROCESS_START_TIMEOUT_MS, &startError))
        {
            if (vProcess->state() != QProcess::NotRunning)
            {
                QString cleanupError;
                StopProcessBounded(*vProcess, 0, PROCESS_KILL_TIMEOUT_MS, &cleanupError);
                if (!cleanupError.isEmpty())
                    startError += QStringLiteral("; cleanup failed: ") + cleanupError;
            }
            kernelStarted = vProcess->state() != QProcess::NotRunning;
            return tr("V2Ray kernel failed to start: %1").arg(startError);
        }

        QString stabilityError;
        if (!ConfirmProcessStable(*vProcess, PROCESS_STABILITY_GRACE_MS, &stabilityError))
        {
            kernelStarted = false;
            return tr("V2Ray kernel exited during startup: %1").arg(stabilityError);
        }

        kernelStarted = true;

        QMap<bool, QMap<QString, QString>> tagProtocolMap;
        for (const auto isOutbound : { GlobalConfig.uiConfig.graphConfig.useOutboundStats, false })
        {
            for (const auto &item : root[isOutbound ? "outbounds" : "inbounds"].toArray())
            {
                const auto tag = item.toObject()["tag"].toString("");
                if (tag == API_TAG_INBOUND)
                    continue;
                if (tag.isEmpty())
                {
                    LOG("Ignored inbound with empty tag.");
                    continue;
                }
                tagProtocolMap[isOutbound][tag] = item.toObject()["protocol"].toString();
            }
        }

        if (QvCoreApplication->StartupArguments.noAPI)
        {
            LOG("API has been disabled by the command line arguments");
        }
        else if (!GlobalConfig.kernelConfig.enableAPI)
        {
            LOG("API has been disabled by the global config option");
        }
        else if (tagProtocolMap.isEmpty())
        {
            LOG("RARE: API is disabled since no inbound tags configured. This is usually caused by a bad complex config.");
        }
        else
        {
            DEBUG("Starting API");
            apiWorker->StartAPI(tagProtocolMap);
            apiEnabled = true;
        }

        return std::nullopt;
    }

    void V2RayKernelInstance::StopConnection()
    {
        if (apiEnabled)
        {
            apiWorker->StopAPI();
            apiEnabled = false;
        }

        // Set this to false before asking the process to stop so the stateChanged
        // callback can distinguish an intentional shutdown from a crash.
        kernelStarted = false;
        QString stopError;
        const auto stopResult = StopProcessBounded(*vProcess, PROCESS_TERMINATE_TIMEOUT_MS, PROCESS_KILL_TIMEOUT_MS, &stopError);
        if (stopResult == ProcessStopResult::Killed)
        {
            LOG("V2Ray kernel did not exit after terminate; killed the process.");
        }
        else if (stopResult == ProcessStopResult::Failed)
        {
            kernelStarted = vProcess->state() != QProcess::NotRunning;
            const auto message = tr("Failed to stop V2Ray kernel process: %1").arg(stopError);
            LOG(message);
            emit OnProcessErrored(message);
        }
    }

    V2RayKernelInstance::~V2RayKernelInstance()
    {
        if (kernelStarted || vProcess->state() != QProcess::NotRunning)
        {
            StopConnection();
        }

        delete apiWorker;
        delete vProcess;
    }

} // namespace Qv2ray::core::kernel
