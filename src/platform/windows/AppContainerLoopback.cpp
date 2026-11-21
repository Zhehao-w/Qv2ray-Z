// The Network Isolation declarations require the Windows 8 SDK target or later.
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#include "AppContainerLoopback.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QStringList>
#include <QUuid>

#include <windows.h>
#include <appmodel.h>
#include <networkisolation.h>
#include <sddl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <userenv.h>

#include <cstring>
#include <memory>
#include <vector>

namespace Qv2ray::platform::windows::loopback
{
    namespace
    {
        constexpr char RequestOption[] = "--windows-loopback-request";
        constexpr char MappingPrefix[] = "Local\\Qv2ray-Z.Loopback.";
        constexpr int MaxRequestSize = 4 * 1024 * 1024;

        const wchar_t *Wide(const QString &text)
        {
            return reinterpret_cast<const wchar_t *>(text.utf16());
        }

        using Handle = std::unique_ptr<void, decltype(&CloseHandle)>;
        using LocalMemory = std::unique_ptr<void, decltype(&LocalFree)>;
        using MappingView = std::unique_ptr<void, decltype(&UnmapViewOfFile)>;

        class NetworkIsolation
        {
          public:
            NetworkIsolation() : library(LoadLibraryExW(L"FirewallAPI.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                if (!library)
                    return;
                enumerate = Resolve<decltype(enumerate)>("NetworkIsolationEnumAppContainers");
                freeApps = Resolve<decltype(freeApps)>("NetworkIsolationFreeAppContainers");
                getConfig = Resolve<decltype(getConfig)>("NetworkIsolationGetAppContainerConfig");
                setConfig = Resolve<decltype(setConfig)>("NetworkIsolationSetAppContainerConfig");
            }

            ~NetworkIsolation()
            {
                if (library)
                    FreeLibrary(library);
            }

            decltype(&NetworkIsolationEnumAppContainers) enumerate = nullptr;
            decltype(&NetworkIsolationFreeAppContainers) freeApps = nullptr;
            decltype(&NetworkIsolationGetAppContainerConfig) getConfig = nullptr;
            decltype(&NetworkIsolationSetAppContainerConfig) setConfig = nullptr;

          private:
            template<typename Function>
            Function Resolve(const char *name) const
            {
                return reinterpret_cast<Function>(GetProcAddress(library, name));
            }

            HMODULE library;
        };

        const NetworkIsolation &Isolation()
        {
            static const NetworkIsolation api;
            return api;
        }

        struct NativeConfig
        {
            ~NativeConfig()
            {
                if (!entries)
                    return;
                const auto heap = GetProcessHeap();
                for (DWORD i = 0; i < count; ++i)
                    if (entries[i].Sid)
                        HeapFree(heap, 0, entries[i].Sid);
                HeapFree(heap, 0, entries);
            }

            DWORD Read()
            {
                if (!Isolation().getConfig)
                    return ERROR_NOT_SUPPORTED;
                const auto error = Isolation().getConfig(&count, &entries);
                return error == ERROR_SUCCESS && count && !entries ? ERROR_INVALID_DATA : error;
            }

            DWORD count = 0;
            SID_AND_ATTRIBUTES *entries = nullptr;
        };

        QString SidText(PSID sid)
        {
            LPWSTR text = nullptr;
            if (!sid || !ConvertSidToStringSidW(sid, &text))
                return {};
            const LocalMemory storage(text, LocalFree);
            return QString::fromWCharArray(text);
        }

        QString NativeText(const wchar_t *text)
        {
            return text ? QString::fromWCharArray(text) : QString();
        }

        bool IsAppContainerSid(PSID sid)
        {
            constexpr SID_IDENTIFIER_AUTHORITY authority = SECURITY_APP_PACKAGE_AUTHORITY;
            if (!sid || !IsValidSid(sid) || std::memcmp(GetSidIdentifierAuthority(sid), &authority, sizeof(authority)) != 0)
                return false;
            const auto count = *GetSidSubAuthorityCount(sid);
            return count >= SECURITY_PARENT_PACKAGE_RID_COUNT && *GetSidSubAuthority(sid, 0) == SECURITY_APP_PACKAGE_BASE_RID;
        }

        QString ParentSid(const QString &sid, PSID nativeSid)
        {
            if (*GetSidSubAuthorityCount(nativeSid) != SECURITY_CHILD_PACKAGE_RID_COUNT)
                return {};
            // The first eight subauthorities identify the parent package.
            return sid.section(QLatin1Char('-'), 0, 2 + SECURITY_PARENT_PACKAGE_RID_COUNT);
        }

        QString PackageFamily(const QString &fullName)
        {
            if (fullName.isEmpty())
                return {};
            UINT32 length = 0;
            if (PackageFamilyNameFromFullName(Wide(fullName), &length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0)
                return {};
            std::vector<wchar_t> family(length);
            if (PackageFamilyNameFromFullName(Wide(fullName), &length, family.data()) == ERROR_SUCCESS)
                return QString::fromWCharArray(family.data());
            return {};
        }

        QString DisplayName(const QString &raw)
        {
            if (!raw.startsWith(QLatin1Char('@')))
                return raw;
            wchar_t buffer[1024];
            if (SUCCEEDED(SHLoadIndirectString(Wide(raw), buffer, ARRAYSIZE(buffer), nullptr)))
                return QString::fromWCharArray(buffer);
            return {};
        }

        QString PackageSid(const QString &family)
        {
            PSID sid = nullptr;
            if (family.isEmpty() || FAILED(DeriveAppContainerSidFromAppContainerName(Wide(family), &sid)))
                return {};
            const auto text = SidText(sid);
            FreeSid(sid);
            return text;
        }

        bool IsElevated()
        {
            HANDLE token = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
                return false;
            const Handle handle(token, CloseHandle);
            TOKEN_ELEVATION elevation{};
            DWORD size = 0;
            return GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
        }

        DWORD Apply(const QSet<QString> &targets, bool enable)
        {
            if (!Isolation().setConfig)
                return ERROR_NOT_SUPPORTED;

            // Serialize read-modify-write across Qv2ray-Z instances and sessions.
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;BA)(A;;GA;;;SY)", SDDL_REVISION_1,
                                                                      &descriptor, nullptr))
                return GetLastError();
            const LocalMemory security(descriptor, LocalFree);
            SECURITY_ATTRIBUTES attributes{ sizeof(attributes), descriptor, FALSE };
            const Handle mutex(CreateMutexW(&attributes, FALSE, L"Global\\Qv2ray-Z.AppContainerLoopback"), CloseHandle);
            if (!mutex)
                return GetLastError();
            const auto wait = WaitForSingleObject(mutex.get(), 15000);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
                return wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            const auto release = [](void *value) { ReleaseMutex(value); };
            const std::unique_ptr<void, decltype(release)> lock(mutex.get(), release);

            QSet<QString> remaining = targets;
            NativeConfig current;
            if (const auto error = current.Read(); error != ERROR_SUCCESS)
                return error;

            // Windows replaces the entire list. Keep the original SID and attributes
            // for every untouched exemption, including SIDs missing from the inventory.
            std::vector<SID_AND_ATTRIBUTES> desired;
            desired.reserve(static_cast<size_t>(current.count) + targets.size());
            for (DWORD i = 0; i < current.count; ++i)
            {
                const auto &entry = current.entries[i];
                const auto sid = SidText(entry.Sid);
                if (sid.isEmpty())
                    return ERROR_INVALID_DATA;
                const bool targeted = targets.contains(sid);
                if (targeted)
                    remaining.remove(sid);
                if (enable || !targeted)
                    desired.push_back(entry);
            }
            if (enable ? remaining.isEmpty() : remaining.size() == targets.size())
                return ERROR_SUCCESS;

            std::vector<LocalMemory> added;
            if (enable)
            {
                for (const auto &sid : remaining)
                {
                    PSID native = nullptr;
                    if (!ConvertStringSidToSidW(Wide(sid), &native))
                        return GetLastError();
                    added.emplace_back(native, LocalFree);
                    if (!IsAppContainerSid(native))
                        return ERROR_INVALID_SID;
                    desired.push_back({ native, 0 });
                }
            }

            SID_AND_ATTRIBUTES empty{};
            return Isolation().setConfig(static_cast<DWORD>(desired.size()), desired.empty() ? &empty : desired.data());
        }

        DWORD RequestElevation(const QSet<QString> &targets, bool enable, HWND parent)
        {
            // Carry the exact approved SID list across UAC, even beyond the command-line
            // length limit. The helper only reads a local mapping and verifies its hash.
            const auto payload = QStringList(targets.values()).join(QLatin1Char('\n')).toLatin1();
            if (payload.size() > MaxRequestSize)
                return ERROR_BUFFER_OVERFLOW;
            const auto mappingName = QString::fromLatin1(MappingPrefix) + QUuid::createUuid().toString(QUuid::Id128);
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)(A;;GR;;;BA)", SDDL_REVISION_1,
                                                                      &descriptor, nullptr))
                return GetLastError();
            const LocalMemory security(descriptor, LocalFree);
            SECURITY_ATTRIBUTES attributes{ sizeof(attributes), descriptor, FALSE };
            const Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, 0, payload.size(),
                                                    Wide(mappingName)),
                                 CloseHandle);
            const auto mappingError = GetLastError();
            if (!mapping || mappingError == ERROR_ALREADY_EXISTS)
                return mappingError;
            {
                const MappingView view(MapViewOfFile(mapping.get(), FILE_MAP_WRITE, 0, 0, payload.size()), UnmapViewOfFile);
                if (!view)
                    return GetLastError();
                std::memcpy(view.get(), payload.constData(), payload.size());
            }

            const auto executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
            const auto parameters = QStringLiteral("%1 %2 %3 %4 %5")
                                        .arg(QString::fromLatin1(RequestOption), mappingName, QString::number(payload.size()),
                                             QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex()),
                                             enable ? QStringLiteral("enable") : QStringLiteral("disable"));
            SHELLEXECUTEINFOW execution{};
            execution.cbSize = sizeof(execution);
            execution.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            execution.hwnd = parent;
            execution.lpVerb = L"runas";
            execution.lpFile = Wide(executable);
            execution.lpParameters = Wide(parameters);
            execution.nShow = SW_HIDE;
            const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            const bool launched = ShellExecuteExW(&execution);
            const auto launchError = launched ? ERROR_SUCCESS : GetLastError();
            if (SUCCEEDED(initialized))
                CoUninitialize();
            const Handle process(execution.hProcess, CloseHandle);
            if (!launched)
                return launchError;
            if (!process)
                return ERROR_INVALID_HANDLE;
            if (WaitForSingleObject(process.get(), INFINITE) != WAIT_OBJECT_0)
                return GetLastError();
            DWORD exitCode = 0;
            return GetExitCodeProcess(process.get(), &exitCode) ? exitCode : GetLastError();
        }
    } // namespace

    Inventory ReadInventory()
    {
        Inventory inventory;
        const auto &api = Isolation();
        if (!api.enumerate || !api.freeApps)
        {
            inventory.error = ERROR_NOT_SUPPORTED;
            return inventory;
        }

        DWORD count = 0;
        PINET_FIREWALL_APP_CONTAINER data = nullptr;
        inventory.error = api.enumerate(NETISO_FLAG_FORCE_COMPUTE_BINARIES, &count, &data);
        if (inventory.error != ERROR_SUCCESS)
        {
            if (data)
                api.freeApps(data);
            count = 0;
            data = nullptr;
            inventory.error = api.enumerate(0, &count, &data);
        }
        const std::unique_ptr<INET_FIREWALL_APP_CONTAINER, decltype(api.freeApps)> entries(data, api.freeApps);
        if (inventory.error != ERROR_SUCCESS)
            return inventory;
        if (count && !data)
        {
            inventory.error = ERROR_INVALID_DATA;
            return inventory;
        }

        QHash<QString, AppContainer> apps;
        QHash<QString, QString> parents;
        QHash<QString, QString> packageGroups;
        for (DWORD i = 0; i < count; ++i)
        {
            const auto &entry = data[i];
            if (!IsAppContainerSid(entry.appContainerSid))
                continue;
            const auto sid = SidText(entry.appContainerSid);
            if (sid.isEmpty())
                continue;
            auto &app = apps[sid];
            app.sid = sid;
            if (app.name.isEmpty())
                app.name = DisplayName(NativeText(entry.displayName));
            if (app.containerName.isEmpty())
                app.containerName = NativeText(entry.appContainerName);
            if (app.packageFullName.isEmpty())
                app.packageFullName = NativeText(entry.packageFullName);

            // A verified package family and a Windows child SID both identify
            // the same app without guessing from names such as "WebView2".
            const auto family = PackageFamily(NativeText(entry.packageFullName));
            if (!family.isEmpty())
            {
                app.groupKey = family;
                const auto packageSid = PackageSid(family);
                if (!packageSid.isEmpty())
                    packageGroups.insert(packageSid, family);
            }
            const auto parent = ParentSid(sid, entry.appContainerSid);
            if (!parent.isEmpty())
                parents.insert(sid, parent);
        }

        for (auto &app : apps)
        {
            const auto parent = parents.value(app.sid);
            if (app.groupKey.isEmpty())
            {
                app.groupKey = packageGroups.value(parent.isEmpty() ? app.sid : parent);
                if (app.groupKey.isEmpty() && !parent.isEmpty() && apps.contains(parent))
                    app.groupKey = apps.value(parent).groupKey;
                if (app.groupKey.isEmpty())
                    app.groupKey = parent.isEmpty() ? app.sid : parent;
            }
            if (app.name.isEmpty())
                app.name = app.containerName.isEmpty() ? app.packageFullName : app.containerName;
            if (app.name.isEmpty())
                app.name = app.sid;
            inventory.apps.append(app);
        }
        return inventory;
    }

    Exemptions ReadExemptions()
    {
        Exemptions result;
        NativeConfig config;
        result.error = config.Read();
        if (result.error != ERROR_SUCCESS)
            return result;
        for (DWORD i = 0; i < config.count; ++i)
        {
            const auto sid = SidText(config.entries[i].Sid);
            if (sid.isEmpty())
            {
                result.sids.clear();
                result.error = ERROR_INVALID_DATA;
                break;
            }
            result.sids.insert(sid);
        }
        return result;
    }

    unsigned long ChangeExemptions(const QSet<QString> &targets, bool enable, quintptr parentWindow)
    {
        const auto current = ReadExemptions();
        if (current.error)
            return current.error;
        if (enable ? (targets - current.sids).isEmpty() : !targets.intersects(current.sids))
            return ERROR_SUCCESS;
        if (IsElevated())
            return Apply(targets, enable);
        return RequestElevation(targets, enable, reinterpret_cast<HWND>(parentWindow));
    }

    QString ErrorMessage(unsigned long error)
    {
        wchar_t buffer[512];
        const auto length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0, buffer,
                                           ARRAYSIZE(buffer), nullptr);
        const auto code = QString::number(error);
        return length ? QStringLiteral("%1 (%2)").arg(QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed(), code)
                      : code;
    }

    std::optional<int> RunElevatedRequest(int argc, char *argv[])
    {
        if (argc < 2 || std::strcmp(argv[1], RequestOption) != 0)
            return std::nullopt;
        if (argc != 6)
            return ERROR_INVALID_PARAMETER;
        if (!IsElevated())
            return ERROR_ELEVATION_REQUIRED;

        const auto mappingName = QString::fromLatin1(argv[2]);
        const auto prefix = QString::fromLatin1(MappingPrefix);
        const auto identifier = mappingName.mid(prefix.size());
        bool validSize = false;
        const auto size = QByteArray(argv[3]).toInt(&validSize);
        const QByteArray hash(argv[4]);
        const QByteArray action(argv[5]);
        if (!mappingName.startsWith(prefix) || identifier.size() != 32 || QByteArray::fromHex(identifier.toLatin1()).toHex() != identifier.toLatin1() ||
            !validSize || size <= 0 || size > MaxRequestSize || hash.size() != 64 || (action != "enable" && action != "disable"))
            return ERROR_INVALID_DATA;

        const Handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, Wide(mappingName)), CloseHandle);
        if (!mapping)
            return static_cast<int>(GetLastError());
        const MappingView view(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, size), UnmapViewOfFile);
        if (!view)
            return static_cast<int>(GetLastError());
        const QByteArray payload(static_cast<const char *>(view.get()), size);
        if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex() != hash)
            return ERROR_INVALID_DATA;
        QSet<QString> targets;
        for (const auto &value : payload.split('\n'))
        {
            const auto sid = QString::fromLatin1(value);
            PSID native = nullptr;
            if (!ConvertStringSidToSidW(Wide(sid), &native))
                return ERROR_INVALID_SID;
            const LocalMemory storage(native, LocalFree);
            if (sid != SidText(native) || (action == "enable" && !IsAppContainerSid(native)))
                return ERROR_INVALID_SID;
            targets.insert(sid);
        }
        return static_cast<int>(Apply(targets, action == "enable"));
    }
} // namespace Qv2ray::platform::windows::loopback
