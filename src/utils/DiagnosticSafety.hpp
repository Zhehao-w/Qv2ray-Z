#pragma once

#include "base/models/QvSettingsObject.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QSysInfo>
#include <QVector>

#include <algorithm>
#include <csignal>

namespace Qv2ray::common::diagnostics
{
    inline QJsonObject BuildSafeDiagnosticInfo(const Qv2ray::base::config::Qv2rayConfigObject *config, int activeKernelCount, int pluginCount)
    {
        QJsonObject info;
        info.insert(QStringLiteral("reportVersion"), 1);
        info.insert(QStringLiteral("application"), QStringLiteral("Qv2ray-Z"));
        info.insert(QStringLiteral("version"), QCoreApplication::applicationVersion());
        info.insert(QStringLiteral("qtVersion"), QString::fromLatin1(qVersion()));
        info.insert(QStringLiteral("operatingSystem"), QSysInfo::prettyProductName());
        info.insert(QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture());
        info.insert(QStringLiteral("activeKernelCount"), std::max(0, activeKernelCount));
        info.insert(QStringLiteral("pluginCount"), std::max(0, pluginCount));
        info.insert(QStringLiteral("configLoaded"), config != nullptr);

        if (config != nullptr)
        {
            // Diagnostics are deliberately allowlisted. Do not serialize the config
            // object itself: it contains credentials, URLs, routes and local paths.
            info.insert(QStringLiteral("configVersion"), config->config_version);
            info.insert(QStringLiteral("networkProxyMode"), static_cast<int>(config->networkConfig.proxyType));
            info.insert(QStringLiteral("kernelApiEnabled"), config->kernelConfig.enableAPI);
        }

        return info;
    }

    inline QString BuildSafeDiagnosticReport(const Qv2ray::base::config::Qv2rayConfigObject *config, int activeKernelCount, int pluginCount)
    {
        QStringList lines;
        lines << QStringLiteral("------- BEGIN QV2RAY DIAGNOSTIC REPORT -------");
        lines << QString::fromUtf8(QJsonDocument(BuildSafeDiagnosticInfo(config, activeKernelCount, pluginCount)).toJson(QJsonDocument::Compact));
        lines << QStringLiteral("------- END OF QV2RAY DIAGNOSTIC REPORT -------");
        return lines.join(QStringLiteral("\r\n"));
    }

    inline QByteArray SafeHttpHeaderValueForLog(const QByteArray &key, const QByteArray &value)
    {
        const auto normalized = key.trimmed().toLower();
        if (normalized == "user-agent" || normalized == "accept" || normalized == "accept-encoding" || normalized == "content-type" ||
            normalized == "content-length")
            return value;
        return QByteArrayLiteral("<redacted>");
    }

    inline QVector<int> FatalSignals()
    {
        QVector<int> signals{ SIGABRT, SIGSEGV };
#ifndef Q_OS_WIN
#ifdef SIGBUS
        signals.append(SIGBUS);
#endif
#ifdef SIGILL
        signals.append(SIGILL);
#endif
#ifdef SIGFPE
        signals.append(SIGFPE);
#endif
#endif
        return signals;
    }

    inline QVector<int> ControlSignals()
    {
#ifndef Q_OS_WIN
        return { SIGTERM, SIGHUP, SIGUSR1, SIGUSR2 };
#else
        return {};
#endif
    }
} // namespace Qv2ray::common::diagnostics
