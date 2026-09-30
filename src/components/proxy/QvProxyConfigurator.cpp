#include "QvProxyConfigurator.hpp"

#include "base/Qv2rayBase.hpp"
#include "components/plugins/QvPluginHost.hpp"
#include "components/proxy/ProxyStateSafety.hpp"
#include "utils/QvHelpers.hpp"
#ifdef Q_OS_WIN
//
#include <Windows.h>
//
#include <WinInet.h>
#include <ras.h>
#include <raserror.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <algorithm>
#include <vector>
#endif

#define QV_MODULE_NAME "SystemProxy"

namespace Qv2ray::components::proxy
{

    using ProcessArgument = QPair<QString, QStringList>;
#ifdef Q_OS_MACOS
    QStringList macOSgetNetworkServices()
    {
        QProcess p;
        p.setProgram("/usr/sbin/networksetup");
        p.setArguments(QStringList{ "-listallnetworkservices" });
        p.start();
        p.waitForStarted();
        p.waitForFinished();
        LOG(p.errorString());
        auto str = p.readAllStandardOutput();
        auto lines = SplitLines(str);
        QStringList result;

        // Start from 1 since first line is unneeded.
        for (auto i = 1; i < lines.count(); i++)
        {
            // * means disabled.
            if (!lines[i].contains("*"))
            {
                result << lines[i];
            }
        }

        LOG("Found " + QSTRN(result.size()) + " network services: " + result.join(";"));
        return result;
    }
#endif
#ifdef Q_OS_WIN
    namespace
    {
        using safety::SystemProxyState;
        constexpr auto PROXY_RECOVERY_SCHEMA = 1;
        constexpr auto PROXY_RECOVERY_FILENAME = "windows-proxy-recovery.json";

        struct WinInetProxyOwnership
        {
            QMap<QString, SystemProxyState> original;
            QMap<QString, SystemProxyState> expected;

            bool active() const
            {
                return !expected.isEmpty();
            }
        };

        WinInetProxyOwnership proxyOwnership;
        safety::ExternalTakeoverLatch proxyTakeoverLatch;
        bool proxyOwnershipLoaded = false;
        bool proxyOwnershipBlocked = false;

        QString ProxyTargetName(const QString &target)
        {
            return target.isEmpty() ? QStringLiteral("LAN") : QStringLiteral("RAS:%1").arg(target);
        }

        QString ProxyRecoveryPath()
        {
            return QvCoreApplication->ConfigPath + QString::fromLatin1(PROXY_RECOVERY_FILENAME);
        }

        void NotifyWinInetProxyChanged()
        {
            InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
            InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
        }

        bool PersistProxyOwnership()
        {
            const auto path = ProxyRecoveryPath();
            if (!proxyOwnership.active())
            {
                if (!QFile::exists(path))
                    return true;
                if (!QFile::remove(path))
                {
                    LOG("Failed to remove completed Windows proxy recovery record: " + path);
                    return false;
                }
                return true;
            }

            QJsonArray entries;
            for (const auto &target : proxyOwnership.expected.keys())
            {
                if (!proxyOwnership.original.contains(target))
                {
                    LOG("Refusing to persist incomplete Windows proxy ownership for " + ProxyTargetName(target));
                    return false;
                }

                QJsonObject entry;
                entry[QStringLiteral("target")] = target;
                entry[QStringLiteral("original")] = safety::SystemProxyStateToJson(proxyOwnership.original[target]);
                entry[QStringLiteral("expected")] = safety::SystemProxyStateToJson(proxyOwnership.expected[target]);
                entries.append(entry);
            }

            QJsonObject root;
            root[QStringLiteral("schema")] = PROXY_RECOVERY_SCHEMA;
            root[QStringLiteral("entries")] = entries;
            const auto payload = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
            if (!StringToFile(payload, path))
            {
                LOG("Failed to persist Windows proxy recovery record: " + path);
                return false;
            }
            return true;
        }

        bool LoadProxyOwnership()
        {
            if (proxyOwnershipLoaded)
                return !proxyOwnershipBlocked;

            proxyOwnershipLoaded = true;
            const auto path = ProxyRecoveryPath();
            if (!QFile::exists(path))
                return true;

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
            {
                LOG("Failed to read Windows proxy recovery record: " + path);
                proxyOwnershipBlocked = true;
                return false;
            }

            const auto payload = file.readAll();
            const auto readError = file.error();
            file.close();
            if (readError != QFile::NoError)
            {
                LOG("Failed while reading Windows proxy recovery record; system proxy changes are blocked: " + path);
                proxyOwnershipBlocked = true;
                return false;
            }

            QJsonParseError parseError;
            const auto document = QJsonDocument::fromJson(payload, &parseError);
            if (parseError.error != QJsonParseError::NoError || !document.isObject())
            {
                LOG("Windows proxy recovery record is invalid; system proxy changes are blocked until it is resolved: " + path);
                proxyOwnershipBlocked = true;
                return false;
            }

            const auto root = document.object();
            if (root[QStringLiteral("schema")].toInt(-1) != PROXY_RECOVERY_SCHEMA || !root[QStringLiteral("entries")].isArray())
            {
                LOG("Windows proxy recovery record has an unsupported schema; system proxy changes are blocked: " + path);
                proxyOwnershipBlocked = true;
                return false;
            }

            WinInetProxyOwnership loaded;
            for (const auto &value : root[QStringLiteral("entries")].toArray())
            {
                if (!value.isObject())
                {
                    LOG("Windows proxy recovery record contains a non-object entry; refusing unsafe recovery.");
                    proxyOwnershipBlocked = true;
                    return false;
                }

                const auto entry = value.toObject();
                if (!entry[QStringLiteral("target")].isString() || !entry[QStringLiteral("original")].isObject() ||
                    !entry[QStringLiteral("expected")].isObject())
                {
                    LOG("Windows proxy recovery record contains an incomplete entry; refusing unsafe recovery.");
                    proxyOwnershipBlocked = true;
                    return false;
                }

                const auto target = entry[QStringLiteral("target")].toString();
                if (loaded.expected.contains(target))
                {
                    LOG("Windows proxy recovery record contains duplicate targets; refusing unsafe recovery.");
                    proxyOwnershipBlocked = true;
                    return false;
                }

                SystemProxyState original;
                SystemProxyState expected;
                if (!safety::SystemProxyStateFromJson(entry[QStringLiteral("original")].toObject(), &original) ||
                    !safety::SystemProxyStateFromJson(entry[QStringLiteral("expected")].toObject(), &expected))
                {
                    LOG("Windows proxy recovery record contains an invalid state for " + ProxyTargetName(target));
                    proxyOwnershipBlocked = true;
                    return false;
                }

                loaded.original[target] = original;
                loaded.expected[target] = expected;
            }

            proxyOwnership = loaded;
            LOG("Loaded Windows proxy recovery record with " + QSTRN(proxyOwnership.expected.size()) + " target(s).");
            return true;
        }

        QString TakeWinInetString(INTERNET_PER_CONN_OPTION &option)
        {
            QString result;
            if (option.Value.pszValue != nullptr)
            {
                result = QString::fromWCharArray(option.Value.pszValue);
                GlobalFree(option.Value.pszValue);
                option.Value.pszValue = nullptr;
            }
            return result;
        }

        void FreeWinInetStrings(INTERNET_PER_CONN_OPTION (&options)[5])
        {
            for (const auto index : { 0, 3, 4 })
            {
                if (options[index].Value.pszValue != nullptr)
                {
                    GlobalFree(options[index].Value.pszValue);
                    options[index].Value.pszValue = nullptr;
                }
            }
        }

        bool QueryWinInetProxyState(const QString &target, SystemProxyState *state)
        {
            INTERNET_PER_CONN_OPTION options[5]{};
            options[0].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
            options[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
#ifdef INTERNET_PER_CONN_FLAGS_UI
            options[2].dwOption = INTERNET_PER_CONN_FLAGS_UI;
#else
            options[2].dwOption = INTERNET_PER_CONN_FLAGS;
#endif
            options[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
            options[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;

            std::wstring targetName = target.toStdWString();
            INTERNET_PER_CONN_OPTION_LIST list{};
            list.dwSize = sizeof(list);
            list.pszConnection = target.isEmpty() ? nullptr : const_cast<wchar_t *>(targetName.c_str());
            list.dwOptionCount = 5;
            list.pOptions = options;

            DWORD size = sizeof(list);
            if (!InternetQueryOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size))
            {
#ifdef INTERNET_PER_CONN_FLAGS_UI
                const auto firstError = GetLastError();
                FreeWinInetStrings(options);
                options[0].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
                options[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
                options[2].dwOption = INTERNET_PER_CONN_FLAGS;
                options[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
                options[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
                list.dwOptionError = 0;
                size = sizeof(list);
                if (!InternetQueryOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size))
                {
                    const auto error = GetLastError();
                    FreeWinInetStrings(options);
                    LOG("InternetQueryOption failed for " + ProxyTargetName(target) + ", GLE=" + QSTRN(error) +
                        ", FLAGS_UI GLE=" + QSTRN(firstError));
                    return false;
                }
#else
                const auto error = GetLastError();
                FreeWinInetStrings(options);
                LOG("InternetQueryOption failed for " + ProxyTargetName(target) + ", GLE=" + QSTRN(error));
                return false;
#endif
            }

            state->flags = options[2].Value.dwValue;
            state->autodiscoveryFlags = options[1].Value.dwValue;
            state->autoConfigUrl = TakeWinInetString(options[0]);
            state->proxyBypass = TakeWinInetString(options[3]);
            state->proxyServer = TakeWinInetString(options[4]);
            return true;
        }

        bool EnumerateWinInetProxyTargets(QStringList *targets)
        {
            targets->clear();
            targets->append(QString()); // Empty target means the LAN settings.

            DWORD size = 0;
            DWORD count = 0;
            auto ret = RasEnumEntries(nullptr, nullptr, nullptr, &size, &count);
            if (ret == ERROR_SUCCESS)
                return true;

            if (ret != ERROR_BUFFER_TOO_SMALL || size < sizeof(RASENTRYNAME))
            {
                LOG("Failed to enumerate RAS entries, error=" + QSTRN(ret));
                return false;
            }

            const auto entryCount = (size + sizeof(RASENTRYNAME) - 1) / sizeof(RASENTRYNAME);
            std::vector<RASENTRYNAME> entries(entryCount);
            entries[0].dwSize = sizeof(RASENTRYNAME);
            ret = RasEnumEntries(nullptr, nullptr, entries.data(), &size, &count);
            if (ret != ERROR_SUCCESS)
            {
                LOG("Failed to enumerate RAS entries, error=" + QSTRN(ret));
                return false;
            }

            for (DWORD index = 0; index < count; ++index)
                targets->append(QString::fromWCharArray(entries[index].szEntryName));

            targets->removeDuplicates();
            return true;
        }

        bool ApplyOwnedManualProxyState(const QString &target, const SystemProxyState &state)
        {
            std::wstring targetName = target.toStdWString();
            std::wstring proxyServer = state.proxyServer.toStdWString();

            INTERNET_PER_CONN_OPTION options[2]{};
            options[0].dwOption = INTERNET_PER_CONN_FLAGS;
            options[0].Value.dwValue = state.flags;
            options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
            options[1].Value.pszValue = const_cast<wchar_t *>(proxyServer.c_str());

            INTERNET_PER_CONN_OPTION_LIST list{};
            list.dwSize = sizeof(list);
            list.pszConnection = target.isEmpty() ? nullptr : const_cast<wchar_t *>(targetName.c_str());
            list.dwOptionCount = 2;
            list.pOptions = options;

            if (!InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list)))
            {
                LOG("InternetSetOption failed for " + ProxyTargetName(target) + ", GLE=" + QSTRN(GetLastError()));
                return false;
            }
            return true;
        }

        bool RestoreWinInetProxyState(const QString &target, const SystemProxyState &state)
        {
            std::wstring targetName = target.toStdWString();
            std::wstring autoConfigUrl = state.autoConfigUrl.toStdWString();
            std::wstring proxyBypass = state.proxyBypass.toStdWString();
            std::wstring proxyServer = state.proxyServer.toStdWString();

            INTERNET_PER_CONN_OPTION options[5]{};
            options[0].dwOption = INTERNET_PER_CONN_FLAGS;
            options[0].Value.dwValue = state.flags;
            options[1].dwOption = INTERNET_PER_CONN_AUTODISCOVERY_FLAGS;
            options[1].Value.dwValue = state.autodiscoveryFlags;
            options[2].dwOption = INTERNET_PER_CONN_AUTOCONFIG_URL;
            options[2].Value.pszValue = const_cast<wchar_t *>(autoConfigUrl.c_str());
            options[3].dwOption = INTERNET_PER_CONN_PROXY_BYPASS;
            options[3].Value.pszValue = const_cast<wchar_t *>(proxyBypass.c_str());
            options[4].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
            options[4].Value.pszValue = const_cast<wchar_t *>(proxyServer.c_str());

            INTERNET_PER_CONN_OPTION_LIST list{};
            list.dwSize = sizeof(list);
            list.pszConnection = target.isEmpty() ? nullptr : const_cast<wchar_t *>(targetName.c_str());
            list.dwOptionCount = 5;
            list.pOptions = options;

            if (!InternetSetOption(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list)))
            {
                LOG("Failed to restore proxy state for " + ProxyTargetName(target) + ", GLE=" + QSTRN(GetLastError()));
                return false;
            }
            return true;
        }

        bool QueryTargetStates(const QStringList &targets, QMap<QString, SystemProxyState> *states)
        {
            states->clear();
            for (const auto &target : targets)
            {
                SystemProxyState state;
                if (!QueryWinInetProxyState(target, &state))
                    return false;
                states->insert(target, state);
            }
            return true;
        }

        bool ResolveFailedSet(const QMap<QString, SystemProxyState> &beforeStates)
        {
            WinInetProxyOwnership unresolved;
            bool notifyNeeded = false;

            for (const auto &target : proxyOwnership.expected.keys())
            {
                SystemProxyState current;
                if (!QueryWinInetProxyState(target, &current))
                {
                    unresolved.original[target] = proxyOwnership.original[target];
                    unresolved.expected[target] = proxyOwnership.expected[target];
                    continue;
                }

                if (beforeStates.contains(target) && current == beforeStates[target])
                    continue;

                if (!safety::IsStillOwned(proxyOwnership.expected[target], current))
                {
                    LOG("Proxy state changed outside Qv2ray during a failed set for " + ProxyTargetName(target) +
                        "; not overwriting the current value.");
                    proxyTakeoverLatch.MarkExternalTakeover();
                    continue;
                }

                const auto original = proxyOwnership.original[target];
                if (!RestoreWinInetProxyState(target, original))
                {
                    unresolved.original[target] = original;
                    unresolved.expected[target] = proxyOwnership.expected[target];
                    continue;
                }

                notifyNeeded = true;
                SystemProxyState restored;
                if (!QueryWinInetProxyState(target, &restored))
                {
                    unresolved.original[target] = original;
                    unresolved.expected[target] = proxyOwnership.expected[target];
                    continue;
                }

                if (restored == original)
                    continue;

                if (safety::IsStillOwned(proxyOwnership.expected[target], restored))
                {
                    unresolved.original[target] = original;
                    unresolved.expected[target] = proxyOwnership.expected[target];
                    continue;
                }

                LOG("Proxy state changed while resolving a failed set for " + ProxyTargetName(target) +
                    "; leaving the observed value untouched and relinquishing ownership.");
                proxyTakeoverLatch.MarkExternalTakeover();
            }

            if (notifyNeeded)
                NotifyWinInetProxyChanged();

            proxyOwnership = unresolved;
            const auto persisted = PersistProxyOwnership();
            if (!persisted)
                LOG("Windows proxy recovery record could not be updated after a failed set.");
            return !proxyOwnership.active() && persisted;
        }

        bool VerifyExistingOwnership()
        {
            bool unchanged = true;
            for (const auto &target : proxyOwnership.expected.keys())
            {
                SystemProxyState current;
                if (!QueryWinInetProxyState(target, &current))
                {
                    LOG("Cannot verify existing proxy ownership for " + ProxyTargetName(target) + "; keeping ownership and aborting the update.");
                    unchanged = false;
                    continue;
                }

                if (!safety::IsStillOwned(proxyOwnership.expected[target], current))
                {
                    LOG("System proxy changed outside Qv2ray for " + ProxyTargetName(target) + "; relinquishing ownership without overwriting it.");
                    proxyTakeoverLatch.MarkExternalTakeover();
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    unchanged = false;
                }
            }

            if (!unchanged && !PersistProxyOwnership())
                LOG("Failed to persist updated Windows proxy ownership after an external change.");
            return unchanged;
        }

        bool SetOwnedWindowsSystemProxy(const QString &proxyServer)
        {
            if (!LoadProxyOwnership())
                return false;

            if (!proxyTakeoverLatch.AllowsAutomaticSet())
            {
                LOG("Automatic Windows system proxy acquisition is blocked because external proxy changes were observed this session.");
                return false;
            }

            if (proxyOwnership.active())
            {
                if (!VerifyExistingOwnership())
                {
                    LOG("System proxy ownership could not be verified unchanged; refusing to reassert Qv2ray proxy settings automatically.");
                    return false;
                }

                QStringList currentTargets;
                if (!EnumerateWinInetProxyTargets(&currentTargets))
                    return false;
                const auto ownershipComplete =
                    currentTargets.size() == proxyOwnership.expected.size() &&
                    std::all_of(currentTargets.cbegin(), currentTargets.cend(), [&](const QString &target) { return proxyOwnership.expected.contains(target); });
                if (!ownershipComplete)
                {
                    LOG("Qv2ray only owns a subset of the current Windows proxy targets; refusing to report the system proxy as configured.");
                    return false;
                }

                const auto requiredFlags = static_cast<quint32>(PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY);
                const auto alreadyConfigured = std::all_of(proxyOwnership.expected.cbegin(), proxyOwnership.expected.cend(),
                                                           [&](const SystemProxyState &state) {
                                                               return state.flags == requiredFlags && state.proxyServer == proxyServer;
                                                           });
                if (alreadyConfigured)
                    return true;

                LOG("Qv2ray already owns Windows proxy state with a different endpoint; clear it before changing the owned endpoint.");
                return false;
            }

            QStringList targets;
            if (!EnumerateWinInetProxyTargets(&targets))
                return false;

            QMap<QString, SystemProxyState> beforeStates;
            if (!QueryTargetStates(targets, &beforeStates))
                return false;

            WinInetProxyOwnership intendedOwnership;
            const auto requiredFlags = static_cast<quint32>(PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY);
            for (const auto &target : targets)
            {
                intendedOwnership.original[target] = beforeStates[target];
                intendedOwnership.expected[target] = safety::MakeOwnedManualProxyState(beforeStates[target], requiredFlags, proxyServer);
            }

            proxyOwnership = intendedOwnership;
            if (!PersistProxyOwnership())
            {
                proxyOwnership = {};
                return false;
            }

            for (const auto &target : targets)
            {
                if (!ApplyOwnedManualProxyState(target, proxyOwnership.expected[target]))
                {
                    ResolveFailedSet(beforeStates);
                    return false;
                }
            }
            NotifyWinInetProxyChanged();

            for (const auto &target : targets)
            {
                SystemProxyState actual;
                if (!QueryWinInetProxyState(target, &actual) || !safety::IsStillOwned(proxyOwnership.expected[target], actual))
                {
                    LOG("Windows proxy write could not be verified for " + ProxyTargetName(target) + "; resolving only targets still owned by Qv2ray.");
                    ResolveFailedSet(beforeStates);
                    return false;
                }
            }
            return true;
        }

        bool ClearOwnedWindowsSystemProxy()
        {
            if (!LoadProxyOwnership())
                return false;

            if (!proxyOwnership.active())
            {
                LOG("Qv2ray does not own the current Windows system proxy; ClearSystemProxy is a no-op.");
                return PersistProxyOwnership();
            }

            bool notifyNeeded = false;
            for (const auto &target : proxyOwnership.expected.keys())
            {
                SystemProxyState current;
                if (!QueryWinInetProxyState(target, &current))
                {
                    LOG("Cannot verify proxy state for " + ProxyTargetName(target) + "; keeping ownership for a later retry.");
                    continue;
                }

                const auto original = proxyOwnership.original[target];
                if (current == original)
                {
                    // This can happen after a crash between restoring a target and
                    // updating/removing the recovery record. Refresh consumers and
                    // mark the target recovered without writing it again.
                    notifyNeeded = true;
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    continue;
                }

                if (!safety::IsStillOwned(proxyOwnership.expected[target], current))
                {
                    LOG("System proxy changed outside Qv2ray for " + ProxyTargetName(target) + "; not restoring the old snapshot.");
                    proxyTakeoverLatch.MarkExternalTakeover();
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    continue;
                }

                if (!RestoreWinInetProxyState(target, original))
                {
                    LOG("Could not restore original proxy state for " + ProxyTargetName(target) + "; ownership retained for retry.");
                    continue;
                }

                notifyNeeded = true;
                SystemProxyState restored;
                if (!QueryWinInetProxyState(target, &restored))
                {
                    LOG("Restored proxy state could not be queried for " + ProxyTargetName(target) + "; ownership retained for retry.");
                    continue;
                }

                if (restored == original)
                {
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    continue;
                }

                if (safety::IsStillOwned(proxyOwnership.expected[target], restored))
                {
                    LOG("Restored proxy state still equals Qv2ray's value for " + ProxyTargetName(target) + "; ownership retained for retry.");
                    continue;
                }

                LOG("Proxy state changed while restoring " + ProxyTargetName(target) +
                    "; leaving the observed value untouched and relinquishing ownership.");
                proxyTakeoverLatch.MarkExternalTakeover();
                proxyOwnership.original.remove(target);
                proxyOwnership.expected.remove(target);
            }

            if (notifyNeeded)
                NotifyWinInetProxyChanged();

            const auto ownershipReleased = !proxyOwnership.active();
            const auto recordPersisted = PersistProxyOwnership();
            return ownershipReleased && recordPersisted;
        }
    } // namespace
#endif

    bool AllowSystemProxyReacquire()
    {
#ifdef Q_OS_WIN
        if (!LoadProxyOwnership())
            return false;

        if (proxyOwnership.active() && !ClearOwnedWindowsSystemProxy())
        {
            LOG("Explicit Windows system proxy enable cannot proceed until remaining owned targets are resolved safely.");
            return false;
        }

        proxyTakeoverLatch.AcknowledgeExplicitEnable();
        LOG("Explicit Windows system proxy enable acknowledged; prior ownership is resolved and automatic acquisition is allowed again for this session.");
#endif
        return true;
    }

    bool RecoverSystemProxyIfNeeded()
    {
#ifdef Q_OS_WIN
        if (!LoadProxyOwnership())
            return false;
        if (!proxyOwnership.active())
            return PersistProxyOwnership();

        LOG("Recovering Windows system proxy ownership left by a previous Qv2ray session.");
        return ClearOwnedWindowsSystemProxy();
#else
        return true;
#endif
    }

    bool SetSystemProxy(const QString &address, int httpPort, int socksPort)
    {
        LOG("Setting up System Proxy");
        bool hasHTTP = (httpPort > 0 && httpPort < 65536);
        bool hasSOCKS = (socksPort > 0 && socksPort < 65536);

#ifdef Q_OS_WIN
        if (!hasHTTP)
        {
            LOG("No valid HTTP inbound is available for the Windows system proxy.");
            return false;
        }
        else
        {
            LOG("Qv2ray will set system proxy to use HTTP");
        }
#else
        if (!hasHTTP && !hasSOCKS)
        {
            LOG("Nothing?");
            return false;
        }

        if (hasHTTP)
        {
            LOG("Qv2ray will set system proxy to use HTTP");
        }

        if (hasSOCKS)
        {
            LOG("Qv2ray will set system proxy to use SOCKS");
        }
#endif

        bool proxySet = true;
#ifdef Q_OS_WIN
        QString proxyAddress;
        const QHostAddress ha(address);
        const auto type = ha.protocol();
        if (type == QAbstractSocket::IPv6Protocol)
        {
            // many software do not recognize IPv6 proxy server string though
            const auto str = ha.toString(); // RFC5952
            proxyAddress = "[" + str + "]:" + QSTRN(httpPort);
        }
        else
        {
            proxyAddress = address + ":" + QSTRN(httpPort);
        }

        LOG("Windows proxy string: " + proxyAddress);
        proxySet = SetOwnedWindowsSystemProxy(proxyAddress);
        if (!proxySet)
        {
            LOG("Windows system proxy was not changed because ownership could not be established safely.");
        }
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy", "mode", "manual" } };
        //
        bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" || qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
        const auto configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        //
        // Configure HTTP Proxies for HTTP, FTP and HTTPS
        if (hasHTTP)
        {
            // iterate over protocols...
            for (const auto &protocol : QStringList{ "http", "ftp", "https" })
            {
                // for GNOME:
                {
                    actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy." + protocol, "host", address } };
                    actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy." + protocol, "port", QSTRN(httpPort) } };
                }

                // for KDE:
                if (isKDE)
                {
                    actions << ProcessArgument{ "kwriteconfig5",
                                                { "--file", configPath + "/kioslaverc", //
                                                  "--group", "Proxy Settings",          //
                                                  "--key", protocol + "Proxy",          //
                                                  "http://" + address + " " + QSTRN(httpPort) } };
                }
            }
        }

        // Configure SOCKS5 Proxies
        if (hasSOCKS)
        {
            // for GNOME:
            {
                actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy.socks", "host", address } };
                actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy.socks", "port", QSTRN(socksPort) } };

                // for KDE:
                if (isKDE)
                {
                    actions << ProcessArgument{ "kwriteconfig5",
                                                { "--file", configPath + "/kioslaverc", //
                                                  "--group", "Proxy Settings",          //
                                                  "--key", "socksProxy",                //
                                                  "socks://" + address + " " + QSTRN(socksPort) } };
                }
            }
        }
        // Setting Proxy Mode to Manual
        {
            // for GNOME:
            {
                actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy", "mode", "manual" } };
            }

            // for KDE:
            if (isKDE)
            {
                actions << ProcessArgument{ "kwriteconfig5",
                                            { "--file", configPath + "/kioslaverc", //
                                              "--group", "Proxy Settings",          //
                                              "--key", "ProxyType", "1" } };
            }
        }

        // Notify kioslaves to reload system proxy configuration.
        if (isKDE)
        {
            actions << ProcessArgument{ "dbus-send",
                                        { "--type=signal", "/KIO/Scheduler",                 //
                                          "org.kde.KIO.Scheduler.reparseSlaveConfiguration", //
                                          "string:''" } };
        }
        // Execute them all!
        //
        // note: do not use std::all_of / any_of / none_of,
        // because those are short-circuit and cannot guarantee atomicity.
        QList<bool> results;
        for (const auto &action : actions)
        {
            // execute and get the code
            const auto returnCode = QProcess::execute(action.first, action.second);
            // print out the commands and result codes
            DEBUG(QString("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
            // give the code back
            results << (returnCode == QProcess::NormalExit);
        }

        if (results.count(true) != actions.size())
        {
            LOG("Something wrong when setting proxies.");
        }
#else

        for (const auto &service : macOSgetNetworkServices())
        {
            LOG("Setting proxy for interface: " + service);
            if (hasHTTP)
            {
                QProcess::execute("/usr/sbin/networksetup", { "-setwebproxystate", service, "on" });
                QProcess::execute("/usr/sbin/networksetup", { "-setsecurewebproxystate", service, "on" });
                QProcess::execute("/usr/sbin/networksetup", { "-setwebproxy", service, address, QSTRN(httpPort) });
                QProcess::execute("/usr/sbin/networksetup", { "-setsecurewebproxy", service, address, QSTRN(httpPort) });
            }

            if (hasSOCKS)
            {
                QProcess::execute("/usr/sbin/networksetup", { "-setsocksfirewallproxystate", service, "on" });
                QProcess::execute("/usr/sbin/networksetup", { "-setsocksfirewallproxy", service, address, QSTRN(socksPort) });
            }
        }

#endif
        if (!proxySet)
            return false;

        //
        // Trigger plugin events
        QMap<Events::SystemProxy::SystemProxyType, int> portSettings;
        if (hasHTTP)
            portSettings.insert(Events::SystemProxy::SystemProxyType::SystemProxy_HTTP, httpPort);
        if (hasSOCKS)
            portSettings.insert(Events::SystemProxy::SystemProxyType::SystemProxy_SOCKS, socksPort);
        PluginHost->SendEvent({ portSettings, Events::SystemProxy::SystemProxyStateType::SetProxy });
        return true;
    }

    bool ClearSystemProxy()
    {
        LOG("Clearing System Proxy");
        bool proxyCleared = true;

#ifdef Q_OS_WIN
        proxyCleared = ClearOwnedWindowsSystemProxy();
        if (!proxyCleared)
        {
            LOG("Some Windows proxy targets remain owned because their original state could not be restored safely.");
        }
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        const bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" || qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
        const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        // Setting System Proxy Mode to: None
        {
            // for GNOME:
            {
                actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy", "mode", "none" } };
            }

            // for KDE:
            if (isKDE)
            {
                actions << ProcessArgument{ "kwriteconfig5",
                                            { "--file", configRoot + "/kioslaverc", //
                                              "--group", "Proxy Settings",          //
                                              "--key", "ProxyType", "0" } };
            }
        }

        // Notify kioslaves to reload system proxy configuration.
        if (isKDE)
        {
            actions << ProcessArgument{ "dbus-send",
                                        { "--type=signal", "/KIO/Scheduler",                 //
                                          "org.kde.KIO.Scheduler.reparseSlaveConfiguration", //
                                          "string:''" } };
        }

        // Execute the Actions
        for (const auto &action : actions)
        {
            // execute and get the code
            const auto returnCode = QProcess::execute(action.first, action.second);
            // print out the commands and result codes
            DEBUG(QString("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
        }

#else
        for (const auto &service : macOSgetNetworkServices())
        {
            LOG("Clearing proxy for interface: " + service);
            QProcess::execute("/usr/sbin/networksetup", { "-setautoproxystate", service, "off" });
            QProcess::execute("/usr/sbin/networksetup", { "-setwebproxystate", service, "off" });
            QProcess::execute("/usr/sbin/networksetup", { "-setsecurewebproxystate", service, "off" });
            QProcess::execute("/usr/sbin/networksetup", { "-setsocksfirewallproxystate", service, "off" });
        }

#endif
        if (proxyCleared)
        {
            //
            // Trigger plugin events
            PluginHost->SendEvent(Events::SystemProxy::EventObject{ {}, Events::SystemProxy::SystemProxyStateType::ClearProxy });
        }
        return proxyCleared;
    }
} // namespace Qv2ray::components::proxy
