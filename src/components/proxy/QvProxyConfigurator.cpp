#include "QvProxyConfigurator.hpp"

#include "base/Qv2rayBase.hpp"
#include "components/plugins/QvPluginHost.hpp"
#include "components/proxy/ProxyStateSafety.hpp"
#include "utils/QvHelpers.hpp"
#ifdef Q_OS_WIN
#include <Windows.h>
#include <WinInet.h>
#include <ras.h>
#include <raserror.h>
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
                result << lines[i];
        }

        LOG("Found " + QSTRN(result.size()) + " network services: " + result.join(";"));
        return result;
    }
#endif

#ifdef Q_OS_WIN
    namespace
    {
        using safety::SystemProxyState;

        struct WinInetProxyOwnership
        {
            QMap<QString, SystemProxyState> original;
            QMap<QString, SystemProxyState> expected;

            bool active() const
            {
                return !expected.isEmpty();
            }

            void clear()
            {
                original.clear();
                expected.clear();
            }
        };

        WinInetProxyOwnership proxyOwnership;

        QString ProxyTargetName(const QString &target)
        {
            return target.isEmpty() ? QStringLiteral("LAN") : QStringLiteral("RAS:%1").arg(target);
        }

        void NotifyWinInetProxyChanged()
        {
            InternetSetOption(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
            InternetSetOption(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
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
            options[2].dwOption = INTERNET_PER_CONN_FLAGS;
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
                const auto error = GetLastError();
                FreeWinInetStrings(options);
                LOG("InternetQueryOption failed for " + ProxyTargetName(target) + ", GLE=" + QSTRN(error));
                return false;
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

            RASENTRYNAME entry{};
            entry.dwSize = sizeof(entry);
            DWORD size = sizeof(entry);
            DWORD count = 0;
            auto ret = RasEnumEntries(nullptr, nullptr, &entry, &size, &count);

            if (ret == ERROR_SUCCESS)
            {
                if (count > 0)
                    targets->append(QString::fromWCharArray(entry.szEntryName));
                targets->removeDuplicates();
                return true;
            }

            if (ret != ERROR_BUFFER_TOO_SMALL)
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

        void PreserveKnownOwnershipAfterRollbackFailure(const QStringList &modifiedTargets, const QMap<QString, SystemProxyState> &originalStates,
                                                        const QMap<QString, SystemProxyState> &intendedStates)
        {
            for (const auto &target : modifiedTargets)
            {
                SystemProxyState current;
                if (!QueryWinInetProxyState(target, &current))
                {
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    continue;
                }

                if (intendedStates.contains(target) && safety::IsStillOwned(intendedStates[target], current))
                {
                    proxyOwnership.original[target] = originalStates[target];
                    proxyOwnership.expected[target] = intendedStates[target];
                }
            }
        }

        bool RollBackCurrentSet(const QStringList &modifiedTargets, const QMap<QString, SystemProxyState> &beforeStates,
                                const QMap<QString, SystemProxyState> &originalStates, const QMap<QString, SystemProxyState> &intendedStates,
                                const WinInetProxyOwnership &previousOwnership)
        {
            bool rollbackSucceeded = true;
            for (const auto &target : modifiedTargets)
            {
                if (!RestoreWinInetProxyState(target, beforeStates[target]))
                    rollbackSucceeded = false;
            }

            if (!modifiedTargets.isEmpty())
                NotifyWinInetProxyChanged();

            if (rollbackSucceeded)
            {
                proxyOwnership = previousOwnership;
                return true;
            }

            proxyOwnership = previousOwnership;
            PreserveKnownOwnershipAfterRollbackFailure(modifiedTargets, originalStates, intendedStates);
            LOG("System proxy rollback was incomplete; only targets still proven to contain Qv2ray's value remain owned.");
            return false;
        }

        bool VerifyExistingOwnership()
        {
            bool lostOwnership = false;
            for (const auto &target : proxyOwnership.expected.keys())
            {
                SystemProxyState current;
                if (!QueryWinInetProxyState(target, &current))
                {
                    LOG("Cannot verify existing proxy ownership for " + ProxyTargetName(target) + "; leaving it untouched.");
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    lostOwnership = true;
                    continue;
                }

                if (!safety::IsStillOwned(proxyOwnership.expected[target], current))
                {
                    LOG("System proxy changed outside Qv2ray for " + ProxyTargetName(target) + "; relinquishing ownership without overwriting it.");
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    lostOwnership = true;
                }
            }
            return !lostOwnership;
        }

        bool SetOwnedWindowsSystemProxy(const QString &proxyServer)
        {
            const auto previousOwnership = proxyOwnership;
            if (proxyOwnership.active() && !VerifyExistingOwnership())
            {
                LOG("System proxy ownership changed externally; refusing to reassert Qv2ray proxy settings automatically.");
                return false;
            }

            QStringList targets;
            if (!EnumerateWinInetProxyTargets(&targets))
                return false;

            QMap<QString, SystemProxyState> beforeStates;
            if (!QueryTargetStates(targets, &beforeStates))
                return false;

            auto originalStates = proxyOwnership.original;
            for (const auto &target : targets)
            {
                if (!originalStates.contains(target))
                    originalStates[target] = beforeStates[target];
            }

            auto intendedStates = proxyOwnership.expected;
            for (const auto &target : targets)
                intendedStates[target] = safety::MakeOwnedManualProxyState(beforeStates[target], PROXY_TYPE_DIRECT | PROXY_TYPE_PROXY, proxyServer);

            QStringList modifiedTargets;
            for (const auto &target : targets)
            {
                if (!ApplyOwnedManualProxyState(target, intendedStates[target]))
                {
                    RollBackCurrentSet(modifiedTargets, beforeStates, originalStates, intendedStates, previousOwnership);
                    return false;
                }
                modifiedTargets.append(target);
            }
            NotifyWinInetProxyChanged();

            for (const auto &target : targets)
            {
                SystemProxyState actual;
                if (!QueryWinInetProxyState(target, &actual) || !safety::IsStillOwned(intendedStates[target], actual))
                {
                    LOG("Windows proxy write could not be verified for " + ProxyTargetName(target) + "; rolling back this set operation.");
                    RollBackCurrentSet(modifiedTargets, beforeStates, originalStates, intendedStates, previousOwnership);
                    return false;
                }
            }

            proxyOwnership.original = originalStates;
            proxyOwnership.expected = intendedStates;
            return true;
        }

        bool ClearOwnedWindowsSystemProxy()
        {
            if (!proxyOwnership.active())
            {
                LOG("Qv2ray does not own the current Windows system proxy; ClearSystemProxy is a no-op.");
                return true;
            }

            bool changed = false;
            for (const auto &target : proxyOwnership.expected.keys())
            {
                SystemProxyState current;
                if (!QueryWinInetProxyState(target, &current))
                {
                    LOG("Cannot verify proxy state for " + ProxyTargetName(target) + "; keeping ownership for a later retry.");
                    continue;
                }

                if (!safety::IsStillOwned(proxyOwnership.expected[target], current))
                {
                    LOG("System proxy changed outside Qv2ray for " + ProxyTargetName(target) + "; not restoring the old snapshot.");
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                    continue;
                }

                const auto original = proxyOwnership.original[target];
                if (!RestoreWinInetProxyState(target, original))
                {
                    LOG("Could not restore original proxy state for " + ProxyTargetName(target) + "; ownership retained for retry.");
                    continue;
                }

                changed = true;
                SystemProxyState restored;
                if (QueryWinInetProxyState(target, &restored) && restored == original)
                {
                    proxyOwnership.original.remove(target);
                    proxyOwnership.expected.remove(target);
                }
                else
                {
                    LOG("Restored proxy state could not be verified for " + ProxyTargetName(target) + "; ownership retained for retry.");
                }
            }

            if (changed)
                NotifyWinInetProxyChanged();

            return !proxyOwnership.active();
        }
    } // namespace
#endif

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
        LOG("Qv2ray will set system proxy to use HTTP");
#else
        if (!hasHTTP && !hasSOCKS)
        {
            LOG("Nothing?");
            return false;
        }

        if (hasHTTP)
            LOG("Qv2ray will set system proxy to use HTTP");
        if (hasSOCKS)
            LOG("Qv2ray will set system proxy to use SOCKS");
#endif

        bool proxySet = true;
#ifdef Q_OS_WIN
        QString proxyAddress;
        const QHostAddress ha(address);
        const auto type = ha.protocol();
        if (type == QAbstractSocket::IPv6Protocol)
        {
            const auto str = ha.toString();
            proxyAddress = "[" + str + "]:" + QSTRN(httpPort);
        }
        else
        {
            proxyAddress = address + ":" + QSTRN(httpPort);
        }

        LOG("Windows proxy string: " + proxyAddress);
        proxySet = SetOwnedWindowsSystemProxy(proxyAddress);
        if (!proxySet)
            LOG("Windows system proxy was not changed because ownership could not be established safely.");
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy", "mode", "manual" } };
        bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" || qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
        const auto configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        if (hasHTTP)
        {
            for (const auto &protocol : QStringList{ "http", "ftp", "https" })
            {
                actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy." + protocol, "host", address } };
                actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy." + protocol, "port", QSTRN(httpPort) } };

                if (isKDE)
                {
                    actions << ProcessArgument{ "kwriteconfig5",
                                                { "--file", configPath + "/kioslaverc", "--group", "Proxy Settings", "--key",
                                                  protocol + "Proxy", "http://" + address + " " + QSTRN(httpPort) } };
                }
            }
        }

        if (hasSOCKS)
        {
            actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy.socks", "host", address } };
            actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy.socks", "port", QSTRN(socksPort) } };

            if (isKDE)
            {
                actions << ProcessArgument{ "kwriteconfig5",
                                            { "--file", configPath + "/kioslaverc", "--group", "Proxy Settings", "--key", "socksProxy",
                                              "socks://" + address + " " + QSTRN(socksPort) } };
            }
        }

        actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy", "mode", "manual" } };
        if (isKDE)
        {
            actions << ProcessArgument{ "kwriteconfig5",
                                        { "--file", configPath + "/kioslaverc", "--group", "Proxy Settings", "--key", "ProxyType", "1" } };
            actions << ProcessArgument{ "dbus-send",
                                        { "--type=signal", "/KIO/Scheduler", "org.kde.KIO.Scheduler.reparseSlaveConfiguration", "string:''" } };
        }

        QList<bool> results;
        for (const auto &action : actions)
        {
            const auto returnCode = QProcess::execute(action.first, action.second);
            DEBUG(QString("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
            results << (returnCode == 0);
        }
        proxySet = results.count(true) == actions.size();
        if (!proxySet)
            LOG("Something wrong when setting proxies.");
#else
        for (const auto &service : macOSgetNetworkServices())
        {
            LOG("Setting proxy for interface: " + service);
            if (hasHTTP)
            {
                proxySet = (QProcess::execute("/usr/sbin/networksetup", { "-setwebproxystate", service, "on" }) == 0) && proxySet;
                proxySet = (QProcess::execute("/usr/sbin/networksetup", { "-setsecurewebproxystate", service, "on" }) == 0) && proxySet;
                proxySet = (QProcess::execute("/usr/sbin/networksetup", { "-setwebproxy", service, address, QSTRN(httpPort) }) == 0) && proxySet;
                proxySet = (QProcess::execute("/usr/sbin/networksetup", { "-setsecurewebproxy", service, address, QSTRN(httpPort) }) == 0) && proxySet;
            }

            if (hasSOCKS)
            {
                proxySet = (QProcess::execute("/usr/sbin/networksetup", { "-setsocksfirewallproxystate", service, "on" }) == 0) && proxySet;
                proxySet = (QProcess::execute("/usr/sbin/networksetup", { "-setsocksfirewallproxy", service, address, QSTRN(socksPort) }) == 0) && proxySet;
            }
        }
#endif

        if (!proxySet)
            return false;

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
            LOG("Some Windows proxy targets remain owned because their original state could not be restored safely.");
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        const bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" || qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
        const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        actions << ProcessArgument{ "gsettings", { "set", "org.gnome.system.proxy", "mode", "none" } };
        if (isKDE)
        {
            actions << ProcessArgument{ "kwriteconfig5",
                                        { "--file", configRoot + "/kioslaverc", "--group", "Proxy Settings", "--key", "ProxyType", "0" } };
            actions << ProcessArgument{ "dbus-send",
                                        { "--type=signal", "/KIO/Scheduler", "org.kde.KIO.Scheduler.reparseSlaveConfiguration", "string:''" } };
        }

        for (const auto &action : actions)
        {
            const auto returnCode = QProcess::execute(action.first, action.second);
            DEBUG(QString("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
            proxyCleared = (returnCode == 0) && proxyCleared;
        }
#else
        for (const auto &service : macOSgetNetworkServices())
        {
            LOG("Clearing proxy for interface: " + service);
            proxyCleared = (QProcess::execute("/usr/sbin/networksetup", { "-setautoproxystate", service, "off" }) == 0) && proxyCleared;
            proxyCleared = (QProcess::execute("/usr/sbin/networksetup", { "-setwebproxystate", service, "off" }) == 0) && proxyCleared;
            proxyCleared = (QProcess::execute("/usr/sbin/networksetup", { "-setsecurewebproxystate", service, "off" }) == 0) && proxyCleared;
            proxyCleared = (QProcess::execute("/usr/sbin/networksetup", { "-setsocksfirewallproxystate", service, "off" }) == 0) && proxyCleared;
        }
#endif

        if (proxyCleared)
            PluginHost->SendEvent(Events::SystemProxy::EventObject{ {}, Events::SystemProxy::SystemProxyStateType::ClearProxy });
        return proxyCleared;
    }
} // namespace Qv2ray::components::proxy
