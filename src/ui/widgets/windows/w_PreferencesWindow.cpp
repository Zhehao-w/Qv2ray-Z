#include "w_PreferencesWindow.hpp"

#include "components/translations/QvTranslator.hpp"
#include "core/connection/ConnectionIO.hpp"
#include "core/handler/ConfigHandler.hpp"
#include "core/kernel/V2RayKernelInteractions.hpp"
#include "core/settings/SettingsBackend.hpp"
#include "src/plugin-interface/QvPluginInterface.hpp"
#include "ui/common/autolaunch/QvAutoLaunch.hpp"
#include "ui/widgets/styles/StyleManager.hpp"
#include "ui/widgets/widgets/DnsSettingsWidget.hpp"
#include "ui/widgets/widgets/RouteSettingsMatrix.hpp"
#include "utils/HTTPRequestHelper.hpp"
#include "utils/QvHelpers.hpp"

#include <QColorDialog>
#include <QCompleter>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHostInfo>
#include <QInputDialog>
#include <QMessageBox>

using Qv2ray::common::validation::IsIPv4Address;
using Qv2ray::common::validation::IsIPv6Address;
using Qv2ray::common::validation::IsValidDNSServer;
using Qv2ray::common::validation::IsValidIPAddress;

#define LOADINGCHECK                                                                                                                                 \
    if (!finishedLoading)                                                                                                                            \
        return;
#define NEEDRESTART                                                                                                                                  \
    LOADINGCHECK                                                                                                                                     \
    if (finishedLoading)                                                                                                                             \
        NeedRestart = true;

#define SET_PROXY_UI_ENABLE(_enabled)                                                                                                                \
    qvProxyTypeCombo->setEnabled(_enabled);                                                                                                          \
    qvProxyAddressTxt->setEnabled(_enabled);                                                                                                         \
    qvProxyPortCB->setEnabled(_enabled);

#define SET_AUTOSTART_UI_ENABLED(_enabled)                                                                                                           \
    autoStartConnCombo->setEnabled(_enabled);                                                                                                        \
    autoStartSubsCombo->setEnabled(_enabled);

#define SET_AUTOSTART_START_MINIMIZED_ENABLED(_enabled) startMinimizedCB->setEnabled(_enabled);

PreferencesWindow::PreferencesWindow(QWidget *parent) : QvDialog("PreferenceWindow", parent), CurrentConfig()
{
    addStateOptions("width", { [&] { return width(); }, [&](QJsonValue val) { resize(val.toInt(), size().height()); } });
    addStateOptions("height", { [&] { return height(); }, [&](QJsonValue val) { resize(size().width(), val.toInt()); } });
    addStateOptions("x", { [&] { return x(); }, [&](QJsonValue val) { move(val.toInt(), y()); } });
    addStateOptions("y", { [&] { return y(); }, [&](QJsonValue val) { move(x(), val.toInt()); } });

    setupUi(this);
    //
    QvMessageBusConnect(PreferencesWindow);
    textBrowser->setHtml(StringFromFile(":/assets/credit.html"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    configdirLabel->setText(QV2RAY_CONFIG_DIR);

    // We add locales
    auto langs = Qv2rayTranslator->GetAvailableLanguages();
    if (!langs.empty())
    {
        languageComboBox->clear();
        languageComboBox->addItems(langs);
    }
    else
    {
        languageComboBox->setDisabled(true);
        // Since we can't have languages detected. It worths nothing to translate these.
        languageComboBox->setToolTip("Cannot find any language providers.");
    }

    // Set auto start button state
    SetAutoStartButtonsState(GetLaunchAtLoginStatus());
    themeCombo->addItems(StyleManager->AllStyles());
    //
    qvVersion->setText(QV2RAY_VERSION_STRING ":" + QSTRN(QV2RAY_VERSION_BUILD));
    qvBuildInfo->setText(QV2RAY_BUILD_INFO);
    qvBuildExInfo->setText(QV2RAY_BUILD_EXTRA_INFO);
    qvBuildTime->setText(__DATE__ " " __TIME__);
    qvPluginInterfaceVersionLabel->setText(tr("Version: %1").arg(QV2RAY_PLUGIN_INTERFACE_VERSION));
    //
    // Deep copy
    CurrentConfig.loadJson(GlobalConfig.toJson());
    //
    themeCombo->setCurrentText(CurrentConfig.uiConfig.theme);
    darkThemeCB->setChecked(CurrentConfig.uiConfig.useDarkTheme);
    darkTrayCB->setChecked(CurrentConfig.uiConfig.useDarkTrayIcon);
    glyphTrayCB->setChecked(CurrentConfig.uiConfig.useGlyphTrayIcon);
    languageComboBox->setCurrentText(CurrentConfig.uiConfig.language);
    logLevelComboBox->setCurrentIndex(CurrentConfig.logLevel);
    quietModeCB->setText(tr("Show connection and proxy status notifications"));
    quietModeCB->setToolTip(tr("Errors, warnings, logs, status indicators and traffic statistics are always shown."));
    quietModeCB->setChecked(!CurrentConfig.uiConfig.quietMode);
    useOldShareLinkFormatCB->setChecked(CurrentConfig.uiConfig.useOldShareLinkFormat);
    // Keep the setting readable for migration, but modern exports no longer need
    // to present this compatibility switch in the everyday preferences UI.
    useOldShareLinkFormatCB->hide();
    // The VMess-era NTP checker is legacy diagnostic UX. Generic application
    // time handling is untouched; only the preference entry point is removed.
    pushButton->hide();
    // browserForwarder was a Qv2ray-specific top-level config object and is no
    // longer accepted by current Xray-core. Retain its model solely so old
    // preferences can be read without data-loss during migration.
    groupBox_2->hide();
    startMinimizedCB->setChecked(CurrentConfig.uiConfig.startMinimized);
    startMinimizedCB->setEnabled(CurrentConfig.autoStartBehavior != AUTO_CONNECTION_NONE);
    exitByCloseEventCB->setChecked(CurrentConfig.uiConfig.exitByCloseEvent);
    //
    //
    listenIPTxt->setText(CurrentConfig.inboundConfig.listenip);
    //
    {
        const auto &httpSettings = CurrentConfig.inboundConfig.httpSettings;
        const auto has_http = CurrentConfig.inboundConfig.useHTTP;
        httpGroupBox->setChecked(has_http);
        httpPortLE->setValue(httpSettings.port);
        httpAuthCB->setChecked(httpSettings.useAuth);
        //
        httpAuthUsernameTxt->setEnabled(has_http && httpSettings.useAuth);
        httpAuthPasswordTxt->setEnabled(has_http && httpSettings.useAuth);
        //
        httpAuthUsernameTxt->setText(httpSettings.account.user);
        httpAuthPasswordTxt->setText(httpSettings.account.pass);
        //
        httpSniffingCB->setChecked(httpSettings.sniffing);
        httpSniffingMetadataOnly->setEnabled(has_http && httpSettings.sniffing);
        httpOverrideHTTPCB->setEnabled(has_http && httpSettings.sniffing);
        httpOverrideTLSCB->setEnabled(has_http && httpSettings.sniffing);
        httpOverrideFakeDNSCB->setEnabled(has_http && httpSettings.sniffing);
        httpOverrideFakeDNSOthersCB->setEnabled(has_http && httpSettings.sniffing);
        httpOverrideHTTPCB->setChecked(httpSettings.destOverride.contains("http"));
        httpOverrideTLSCB->setChecked(httpSettings.destOverride.contains("tls"));
        httpOverrideFakeDNSCB->setChecked(httpSettings.destOverride.contains("fakedns"));
        httpOverrideFakeDNSOthersCB->setChecked(httpSettings.destOverride.contains("fakedns+others"));
        httpSniffingMetadataOnly->setChecked(httpSettings.metadataOnly);
    }
    {
        const auto &socksSettings = CurrentConfig.inboundConfig.socksSettings;
        const auto has_socks = CurrentConfig.inboundConfig.useSocks;
        socksGroupBox->setChecked(has_socks);
        socksPortLE->setValue(socksSettings.port);
        //
        socksAuthCB->setChecked(socksSettings.useAuth);
        socksAuthUsernameTxt->setEnabled(has_socks && socksSettings.useAuth);
        socksAuthPasswordTxt->setEnabled(has_socks && socksSettings.useAuth);
        socksAuthUsernameTxt->setText(socksSettings.account.user);
        socksAuthPasswordTxt->setText(socksSettings.account.pass);
        // Socks UDP Options
        socksUDPCB->setChecked(socksSettings.enableUDP);
        socksUDPIP->setEnabled(has_socks && socksSettings.enableUDP);
        socksUDPIP->setText(socksSettings.localIP);
        //
        socksSniffingCB->setChecked(socksSettings.sniffing);
        socksSniffingMetadataOnly->setEnabled(has_socks && socksSettings.sniffing);
        socksOverrideHTTPCB->setEnabled(has_socks && socksSettings.sniffing);
        socksOverrideTLSCB->setEnabled(has_socks && socksSettings.sniffing);
        socksOverrideFakeDNSCB->setEnabled(has_socks && socksSettings.sniffing);
        socksOverrideFakeDNSOthersCB->setEnabled(has_socks && socksSettings.sniffing);
        socksOverrideHTTPCB->setChecked(socksSettings.destOverride.contains("http"));
        socksOverrideTLSCB->setChecked(socksSettings.destOverride.contains("tls"));
        socksOverrideFakeDNSCB->setChecked(socksSettings.destOverride.contains("fakedns"));
        socksOverrideFakeDNSOthersCB->setChecked(socksSettings.destOverride.contains("fakedns+others"));
        socksSniffingMetadataOnlyCB->setChecked(socksSettings.metadataOnly);
    }
    {
        const auto &tProxySettings = CurrentConfig.inboundConfig.tProxySettings;
        const auto has_tproxy = CurrentConfig.inboundConfig.useTPROXY;
        tproxyGroupBox->setChecked(has_tproxy);
        tproxyListenAddr->setText(tProxySettings.tProxyIP);
        tproxyListenV6Addr->setText(tProxySettings.tProxyV6IP);
        tProxyPort->setValue(tProxySettings.port);
        tproxyEnableTCP->setChecked(tProxySettings.hasTCP);
        tproxyEnableUDP->setChecked(tProxySettings.hasUDP);
        //
        tproxySniffingCB->setChecked(tProxySettings.sniffing);
        tproxySniffingMetadataOnlyCB->setEnabled(has_tproxy && tProxySettings.sniffing);
        tproxyOverrideHTTPCB->setEnabled(has_tproxy && tProxySettings.sniffing);
        tproxyOverrideTLSCB->setEnabled(has_tproxy && tProxySettings.sniffing);
        tproxyOverrideFakeDNSCB->setEnabled(has_tproxy && tProxySettings.sniffing);
        tproxyOverrideFakeDNSOthersCB->setEnabled(has_tproxy && tProxySettings.sniffing);
        tproxyOverrideHTTPCB->setChecked(tProxySettings.destOverride.contains("http"));
        tproxyOverrideTLSCB->setChecked(tProxySettings.destOverride.contains("tls"));
        tproxyOverrideFakeDNSCB->setChecked(tProxySettings.destOverride.contains("fakedns"));
        tproxyOverrideFakeDNSOthersCB->setChecked(tProxySettings.destOverride.contains("fakedns+others"));
        tproxySniffingMetadataOnlyCB->setChecked(tProxySettings.metadataOnly);

        tproxyMode->setCurrentText(tProxySettings.mode);
    }
    {
        const auto &browserForwarderSettings = CurrentConfig.inboundConfig.browserForwarderSettings;
        browserForwarderAddressTxt->setText(browserForwarderSettings.address);
        browserForwarderPortSB->setValue(browserForwarderSettings.port);
    }
    outboundMark->setValue(CurrentConfig.outboundConfig.mark);
    //
    dnsIntercept->setChecked(CurrentConfig.defaultRouteConfig.connectionConfig.dnsIntercept);
    dnsFreedomCb->setChecked(CurrentConfig.defaultRouteConfig.connectionConfig.v2rayFreedomDNS);
    //
    // Kernel Settings
    {
        const auto paths =
            V2RayKernelInstance::EffectiveKernelPaths(CurrentConfig.kernelConfig.KernelPath(), CurrentConfig.kernelConfig.AssetsPath());
        vCorePathTxt->setText(paths.executable);
        vCoreAssetsPathTxt->setText(paths.assets);
        enableAPI->setChecked(CurrentConfig.kernelConfig.enableAPI);
        statsPortBox->setValue(CurrentConfig.kernelConfig.statsPort);
        //
        V2RayOutboundStatsCB->setChecked(CurrentConfig.uiConfig.graphConfig.useOutboundStats);
        hasDirectStatisticsCB->setEnabled(CurrentConfig.uiConfig.graphConfig.useOutboundStats);
        hasDirectStatisticsCB->setChecked(CurrentConfig.uiConfig.graphConfig.hasDirectStats);
        //
        pluginKernelV2RayIntegrationCB->setChecked(CurrentConfig.pluginConfig.v2rayIntegration);
        pluginKernelPortAllocateCB->setValue(CurrentConfig.pluginConfig.portAllocationStart);
        pluginKernelPortAllocateCB->setEnabled(CurrentConfig.pluginConfig.v2rayIntegration);
    }
    // Connection Settings
    {
        bypassCNCb->setChecked(CurrentConfig.defaultRouteConfig.connectionConfig.bypassCN);
        bypassBTCb->setChecked(CurrentConfig.defaultRouteConfig.connectionConfig.bypassBT);
        proxyDefaultCb->setChecked(!CurrentConfig.defaultRouteConfig.connectionConfig.enableProxy);
        bypassPrivateCb->setChecked(CurrentConfig.defaultRouteConfig.connectionConfig.bypassLAN);

        auto *routingMode = new QComboBox(groupBox);
        routingMode->setObjectName("routingModeCombo");
        routingMode->addItem(tr("Global Proxy"), QvConfig_Connection::GlobalProxy);
        routingMode->addItem(tr("Bypass Mainland China (Recommended in mainland China)"), QvConfig_Connection::BypassMainlandChina);
        routingMode->addItem(tr("Direct"), QvConfig_Connection::Direct);
        routingMode->addItem(tr("Custom"), QvConfig_Connection::Custom);
        const auto &legacyRoute = CurrentConfig.defaultRouteConfig.connectionConfig;
        int effectiveMode = legacyRoute.routingMode;
        // Infer presets from the long-standing flags as well. This prevents a
        // newly introduced UI preference from changing an older configuration.
        if (!legacyRoute.enableProxy)
            effectiveMode = QvConfig_Connection::Direct;
        else if (legacyRoute.bypassCN && legacyRoute.bypassLAN)
            effectiveMode = QvConfig_Connection::BypassMainlandChina;
        else if (!legacyRoute.bypassCN && legacyRoute.bypassLAN)
            effectiveMode = QvConfig_Connection::GlobalProxy;
        else
            effectiveMode = QvConfig_Connection::Custom;
        routingMode->setCurrentIndex(routingMode->findData(effectiveMode));
        auto *summary = new QLabel(groupBox);
        summary->setWordWrap(true);
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        formLayout->insertRow(0, tr("Routing Mode"), routingMode);
        formLayout->insertRow(1, tr("Effective Routing"), summary);

        const auto updateRoutingMode = [this, routingMode, summary](int) {
            auto &connection = CurrentConfig.defaultRouteConfig.connectionConfig;
            const auto mode = routingMode->currentData().toInt();
            connection.routingMode = mode;
            const bool custom = mode == QvConfig_Connection::Custom;
            label_16->setVisible(custom);
            proxyDefaultCb->setVisible(custom);
            label_41->setVisible(custom);
            bypassPrivateCb->setVisible(custom);
            label_17->setVisible(custom);
            bypassCNCb->setVisible(custom);
            if (!custom)
            {
                connection.enableProxy = mode != QvConfig_Connection::Direct;
                connection.bypassCN = mode == QvConfig_Connection::BypassMainlandChina;
                connection.bypassLAN = true;
            }
            if (mode == QvConfig_Connection::GlobalProxy)
                summary->setText(tr("Private/LAN → Direct; everything else → Proxy."));
            else if (mode == QvConfig_Connection::BypassMainlandChina)
                summary->setText(tr("Private/LAN → Direct; custom Block, Proxy and Direct rules take precedence; geoip:cn and geosite:cn → Direct; everything else → Proxy."));
            else if (mode == QvConfig_Connection::Direct)
                summary->setText(tr("Internet traffic → Direct. Advanced rules are retained for later use."));
            else
                summary->setText(tr("Advanced routing controls and explicit rule order are used unchanged."));
        };
        connect(routingMode, QOverload<int>::of(&QComboBox::currentIndexChanged), updateRoutingMode);
        updateRoutingMode(routingMode->currentIndex());
    }
    //
    //
    latencyTCPingRB->setChecked(CurrentConfig.networkConfig.latencyTestingMethod == TCPING);
    latencyICMPingRB->setChecked(CurrentConfig.networkConfig.latencyTestingMethod == ICMPING);
    latencyRealPingTestURLTxt->setText(CurrentConfig.networkConfig.latencyRealPingTestURL);
    //
    {
        qvProxyPortCB->setValue(CurrentConfig.networkConfig.port);
        qvProxyAddressTxt->setText(CurrentConfig.networkConfig.address);
        qvProxyTypeCombo->setCurrentText(CurrentConfig.networkConfig.type);
        qvNetworkUATxt->setEditText(CurrentConfig.networkConfig.userAgent);
        //
        qvProxyNoProxy->setChecked(CurrentConfig.networkConfig.proxyType == Qv2rayConfig_Network::QVPROXY_NONE);
        qvProxySystemProxy->setChecked(CurrentConfig.networkConfig.proxyType == Qv2rayConfig_Network::QVPROXY_SYSTEM);
        qvProxyCustomProxy->setChecked(CurrentConfig.networkConfig.proxyType == Qv2rayConfig_Network::QVPROXY_CUSTOM);
        SET_PROXY_UI_ENABLE(CurrentConfig.networkConfig.proxyType == Qv2rayConfig_Network::QVPROXY_CUSTOM)
    }
    //
    //
    //
    // Advanced config.
    {
        setTestLatencyCB->setChecked(CurrentConfig.advancedConfig.testLatencyPeriodically);
        setTestLatencyOnConnectedCB->setChecked(CurrentConfig.advancedConfig.testLatencyOnConnected);
        disableSystemRootCB->setChecked(CurrentConfig.advancedConfig.disableSystemRoot);
    }
    //
    {
        if (CurrentConfig.autoStartId >= 0 && CurrentConfig.autoStartId < ConnectionManager->connections.count())
            autoStartConnCombo->setCurrentText(ConnectionManager->connections[CurrentConfig.autoStartId].name);
        else
            CurrentConfig.autoStartId = AUTO_CONNECTION_ID_NONE;
    }
    //
    {
        if (!GlobalConfig.subscriptions.isEmpty())
        {
            autoStartSubsCombo->addItem(tr("None"));
            autoStartSubsCombo->addItems(GlobalConfig.subscriptions.values());
        }
    }
    switch (CurrentConfig.autoStartBehavior)
    {
        case AUTO_CONNECTION_NONE:
            noAutoConnectRB->setChecked(true);
            break;
        case AUTO_CONNECTION_FIXED:
            autoConnectRB->setChecked(true);
            break;
        case AUTO_CONNECTION_LAST_CONNECTED:
            autoLastConnectedRB->setChecked(true);
            break;
    }
    SET_AUTOSTART_UI_ENABLED(CurrentConfig.autoStartBehavior == AUTO_CONNECTION_FIXED)

    //
    // System Proxy Settings
    {
        systemProxyTypeComboBox->addItem(tr("Automatic (PAC)"), Qv2rayConfig_SystemProxy::PAC_PROXY);
        systemProxyTypeComboBox->addItem(tr("Global"), Qv2rayConfig_SystemProxy::GLOBAL_PROXY);
        const auto &systemProxySettings = CurrentConfig.inboundConfig.systemProxySettings;
        systemProxyTypeComboBox->setCurrentIndex(systemProxyTypeComboBox->findData(systemProxySettings.proxyType));
    }

    //
    // Quiet mode
    //
    // Tabs
    // Removed legacy General settings that map to deprecated/obsolete Xray behavior.
    // Keep Networking, Inbound, Kernel, Connections, Subscription, Advanced and About.
    while (tabWidget->count() > 0)
    {
        bool removed = false;
        for (int i = 0; i < tabWidget->count(); ++i)
        {
            const auto label = tabWidget->tabText(i).toLower();
            if (label.contains("general") || label.contains("通用") || label.contains("一般") || label.contains("общ"))
            {
                tabWidget->removeTab(i);
                removed = true;
                break;
            }
        }
        if (!removed)
            break;
    }
    //
    UpdateColorScheme();
}

void PreferencesWindow::UpdateColorScheme()
{
    textBrowser->setStyleSheet("QTextBrowser{background-color: rgba(0,0,0,0)}");
}

PreferencesWindow::~PreferencesWindow()
{
}

void PreferencesWindow::SaveCurrentConfig()
{
    // Inbound Settings
    {
        CurrentConfig.inboundConfig.listenip = listenIPTxt->text();
        CurrentConfig.inboundConfig.httpSettings.port = httpPortLE->value();
        CurrentConfig.inboundConfig.httpSettings.useAuth = httpAuthCB->isChecked();
        CurrentConfig.inboundConfig.httpSettings.account.user = httpAuthUsernameTxt->text();
        CurrentConfig.inboundConfig.httpSettings.account.pass = httpAuthPasswordTxt->text();
        //
        CurrentConfig.inboundConfig.httpSettings.sniffing = httpSniffingCB->isChecked();
        CurrentConfig.inboundConfig.httpSettings.destOverride.clear();
        if (httpOverrideHTTPCB->isChecked())
            CurrentConfig.inboundConfig.httpSettings.destOverride << "http";
        if (httpOverrideTLSCB->isChecked())
            CurrentConfig.inboundConfig.httpSettings.destOverride << "tls";
        if (httpOverrideFakeDNSCB->isChecked())
            CurrentConfig.inboundConfig.httpSettings.destOverride << "fakedns";
        if (httpOverrideFakeDNSOthersCB->isChecked())
            CurrentConfig.inboundConfig.httpSettings.destOverride << "fakedns+others";
    }
    {
        CurrentConfig.inboundConfig.socksSettings.port = socksPortLE->value();
        CurrentConfig.inboundConfig.socksSettings.useAuth = socksAuthCB->isChecked();
        CurrentConfig.inboundConfig.socksSettings.account.user = socksAuthUsernameTxt->text();
        CurrentConfig.inboundConfig.socksSettings.account.pass = socksAuthPasswordTxt->text();
        //
        CurrentConfig.inboundConfig.socksSettings.sniffing = socksSniffingCB->isChecked();
        CurrentConfig.inboundConfig.socksSettings.destOverride.clear();
        if (socksOverrideHTTPCB->isChecked())
            CurrentConfig.inboundConfig.socksSettings.destOverride << "http";
        if (socksOverrideTLSCB->isChecked())
            CurrentConfig.inboundConfig.socksSettings.destOverride << "tls";
        if (socksOverrideFakeDNSCB->isChecked())
            CurrentConfig.inboundConfig.socksSettings.destOverride << "fakedns";
        if (socksOverrideFakeDNSOthersCB->isChecked())
            CurrentConfig.inboundConfig.socksSettings.destOverride << "fakedns+others";
        //
        CurrentConfig.inboundConfig.socksSettings.enableUDP = socksUDPCB->isChecked();
        CurrentConfig.inboundConfig.socksSettings.localIP = socksUDPIP->text();
    }
    {
        CurrentConfig.inboundConfig.tProxySettings.tProxyIP = tproxyListenAddr->text();
        CurrentConfig.inboundConfig.tProxySettings.tProxyV6IP = tproxyListenV6Addr->text();
        CurrentConfig.inboundConfig.tProxySettings.port = tProxyPort->value();
        CurrentConfig.inboundConfig.tProxySettings.hasTCP = tproxyEnableTCP->isChecked();
        CurrentConfig.inboundConfig.tProxySettings.hasUDP = tproxyEnableUDP->isChecked();
        //
        CurrentConfig.inboundConfig.tProxySettings.sniffing = tproxySniffingCB->isChecked();
        CurrentConfig.inboundConfig.tProxySettings.destOverride.clear();
        if (tproxyOverrideHTTPCB->isChecked())
            CurrentConfig.inboundConfig.tProxySettings.destOverride << "http";
        if (tproxyOverrideTLSCB->isChecked())
            CurrentConfig.inboundConfig.tProxySettings.destOverride << "tls";
        if (tproxyOverrideFakeDNSCB->isChecked())
            CurrentConfig.inboundConfig.tProxySettings.destOverride << "fakedns";
        if (tproxyOverrideFakeDNSOthersCB->isChecked())
            CurrentConfig.inboundConfig.tProxySettings.destOverride << "fakedns+others";
        tproxySniffingMetadataOnlyCB->setChecked(CurrentConfig.inboundConfig.tProxySettings.metadataOnly);

        CurrentConfig.inboundConfig.tProxySettings.mode = tproxyMode->currentText();
    }
    CurrentConfig.inboundConfig.browserForwarderSettings.address = browserForwarderAddressTxt->text();
    CurrentConfig.inboundConfig.browserForwarderSettings.port = browserForwarderPortSB->value();
    CurrentConfig.outboundConfig.mark = outboundMark->value();
    CurrentConfig.defaultRouteConfig.connectionConfig.dnsIntercept = dnsIntercept->isChecked();
    CurrentConfig.defaultRouteConfig.connectionConfig.v2rayFreedomDNS = dnsFreedomCb->isChecked();
    //
    // Kernel settings
    CurrentConfig.kernelConfig.SetKernelPath(vCorePathTxt->text(), vCoreAssetsPathTxt->text());
    CurrentConfig.kernelConfig.enableAPI = enableAPI->isChecked();
    CurrentConfig.kernelConfig.statsPort = statsPortBox->value();
    CurrentConfig.uiConfig.graphConfig.useOutboundStats = V2RayOutboundStatsCB->isChecked();
    CurrentConfig.uiConfig.graphConfig.hasDirectStats = hasDirectStatisticsCB->isChecked();
    //
    CurrentConfig.pluginConfig.v2rayIntegration = pluginKernelV2RayIntegrationCB->isChecked();
    CurrentConfig.pluginConfig.portAllocationStart = pluginKernelPortAllocateCB->value();
    // Connection Settings
    CurrentConfig.defaultRouteConfig.connectionConfig.bypassCN = bypassCNCb->isChecked();
    CurrentConfig.defaultRouteConfig.connectionConfig.bypassBT = bypassBTCb->isChecked();
    CurrentConfig.defaultRouteConfig.connectionConfig.bypassLAN = bypassPrivateCb->isChecked();
    //
    //
    CurrentConfig.networkConfig.latencyTestingMethod = latencyTCPingRB->isChecked() ? TCPING : (latencyICMPingRB->isChecked() ? ICMPING : REALPING);
    CurrentConfig.networkConfig.latencyRealPingTestURL = latencyRealPingTestURLTxt->text();
    //
    //
    //
    CurrentConfig.uiConfig.useOldShareLinkFormat = useOldShareLinkFormatCB->isChecked();
    CurrentConfig.uiConfig.startMinimized = startMinimizedCB->isChecked();
    CurrentConfig.uiConfig.exitByCloseEvent = exitByCloseEventCB->isChecked();
    //
    //
    // Advanced settings.
    CurrentConfig.advancedConfig.testLatencyPeriodically = setTestLatencyCB->isChecked();
    CurrentConfig.advancedConfig.testLatencyOnConnected = setTestLatencyOnConnectedCB->isChecked();
    CurrentConfig.advancedConfig.disableSystemRoot = disableSystemRootCB->isChecked();
    //
    // UI settings
    CurrentConfig.uiConfig.language = languageComboBox->currentText();
    CurrentConfig.uiConfig.theme = themeCombo->currentText();
    CurrentConfig.uiConfig.useDarkTheme = darkThemeCB->isChecked();
    CurrentConfig.uiConfig.useDarkTrayIcon = darkTrayCB->isChecked();
    CurrentConfig.uiConfig.useGlyphTrayIcon = glyphTrayCB->isChecked();
    CurrentConfig.logLevel = LogType(logLevelComboBox->currentIndex());
    //
    CurrentConfig.uiConfig.quietMode = !quietModeCB->isChecked();
    //
    // Auto start settings
    //
    if (noAutoConnectRB->isChecked())
        CurrentConfig.autoStartBehavior = AUTO_CONNECTION_NONE;
    else if (autoConnectRB->isChecked())
        CurrentConfig.autoStartBehavior = AUTO_CONNECTION_FIXED;
    else if (autoLastConnectedRB->isChecked())
        CurrentConfig.autoStartBehavior = AUTO_CONNECTION_LAST_CONNECTED;
    CurrentConfig.autoStartId = autoStartConnCombo->currentIndex();
    //
    // System Proxy settings
    CurrentConfig.inboundConfig.systemProxySettings.proxyType = Qv2rayConfig_SystemProxy::ProxyType(systemProxyTypeComboBox->currentData().toInt());
    //
    // Network Settings
    {
        CurrentConfig.networkConfig.type = qvProxyTypeCombo->currentText();
        CurrentConfig.networkConfig.address = qvProxyAddressTxt->text();
        CurrentConfig.networkConfig.port = qvProxyPortCB->value();
        CurrentConfig.networkConfig.proxyType = qvProxyNoProxy->isChecked()
                                                    ? Qv2rayConfig_Network::QVPROXY_NONE
                                                    : (qvProxySystemProxy->isChecked() ? Qv2rayConfig_Network::QVPROXY_SYSTEM : Qv2rayConfig_Network::QVPROXY_CUSTOM);
        CurrentConfig.networkConfig.userAgent = qvNetworkUATxt->currentText();
    }
    //
    GlobalConfig.loadJson(CurrentConfig.toJson());
    SaveGlobalSettings();
}

void PreferencesWindow::on_autoLastConnectedRB_clicked()
{
    NEEDRESTART
    autoStartConnCombo->setDisabled(true);
}

void PreferencesWindow::on_autoConnectRB_clicked()
{
    NEEDRESTART
    autoStartConnCombo->setEnabled(true);
}

void PreferencesWindow::on_recheckCNConnectionBtn_clicked()
{
    QvMessageBoxInfo(this, tr("Maintenance Mode"), tr("Legacy connectivity probes are no longer part of the supported Windows product path."));
}

void PreferencesWindow::on_pushButton_clicked()
{
#if QV2RAY_FEATURE(util_has_ntp)
    const auto ntpTitle = tr("NTP Checker");
    const auto ntpHint = tr("Check date and time from server:");
    const static QStringList ntpServerList = { "cn.pool.ntp.org",      "cn.ntp.org.cn",           "edu.ntp.org.cn",
                                               "time.pool.aliyun.com", "time1.cloud.tencent.com", "ntp.neu.edu.cn" };
    bool ok = false;
    const auto ntpServer = QInputDialog::getItem(this, ntpTitle, ntpHint, ntpServerList, 0, true, &ok).trimmed();
    if (!ok)
        return;
    auto client = new QvNTPClient(this);
    connect(client, &QvNTPClient::timeUpdated, this,
            [this, client, ntpTitle](QDateTime time) { QvMessageBoxInfo(this, ntpTitle, tr("Time: %1").arg(time.toString())); });
    LOG("Getting host info: " + ntpServer)
    auto hostInfo = QHostInfo::fromName(ntpServer);
    if (hostInfo.error() == QHostInfo::NoError)
        client->sendRequest(hostInfo.addresses().first(), 123);
    else
        QvMessageBoxWarn(this, ntpTitle, tr("Failed to lookup server: %1").arg(hostInfo.errorString()));
#else
    QvMessageBoxWarn(this, tr("No NTP Backend"), tr("Qv2ray was not built with NTP support."));
#endif
}

void PreferencesWindow::on_noAutoConnectRB_clicked()
{
    NEEDRESTART
    autoStartConnCombo->setDisabled(true);
}

void PreferencesWindow::on_checkVCoreVersion_clicked()
{
    const auto paths = V2RayKernelInstance::EffectiveKernelPaths(CurrentConfig.kernelConfig.KernelPath(), CurrentConfig.kernelConfig.AssetsPath());
    const auto kernelPath = paths.executable;
    if (!kernelPath.isEmpty())
    {
        QProcess process;
        process.start(kernelPath, { "version" });
        if (!process.waitForStarted(3000))
        {
            QvMessageBoxWarn(this, tr("Kernel Version"), tr("Failed to start Xray: %1").arg(process.errorString()));
            return;
        }
        if (!process.waitForFinished(5000))
        {
            process.kill();
            process.waitForFinished(1000);
            QvMessageBoxWarn(this, tr("Kernel Version"), tr("Timed out while checking Xray version."));
            return;
        }
        const auto output = QString::fromUtf8(process.readAllStandardOutput()) + QString::fromUtf8(process.readAllStandardError());
        QvMessageBoxInfo(this, tr("Kernel Version"), output.trimmed());
    }
    else
    {
        QvMessageBoxWarn(this, tr("Kernel Version"), tr("Cannot Find Xray Core"));
    }
}

void PreferencesWindow::on_pluginKernelV2RayIntegrationCB_stateChanged(int arg1)
{
    NEEDRESTART
    pluginKernelPortAllocateCB->setEnabled(arg1 == Qt::Checked);
}

void PreferencesWindow::on_connectionReorderButtons_clicked()
{
    QvMessageBoxInfo(this, tr("Connection Order"), tr("Connection reordering is no longer exposed by the maintained Windows interface."));
}

void PreferencesWindow::on_languageComboBox_currentTextChanged(const QString &arg1)
{
    LOADINGCHECK
    CurrentConfig.uiConfig.language = arg1;
    NeedRestart = true;
}

void PreferencesWindow::on_themeCombo_currentTextChanged(const QString &arg1)
{
    LOADINGCHECK
    CurrentConfig.uiConfig.theme = arg1;
    NeedRestart = true;
}

void PreferencesWindow::on_darkThemeCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.uiConfig.useDarkTheme = arg1 == Qt::Checked;
    QvMessageBusEmit(ChangeColorScheme);
}

void PreferencesWindow::on_darkTrayCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.uiConfig.useDarkTrayIcon = arg1 == Qt::Checked;
    QvMessageBusEmit(ChangeColorScheme);
}

void PreferencesWindow::on_glyphTrayCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.uiConfig.useGlyphTrayIcon = arg1 == Qt::Checked;
    QvMessageBusEmit(ChangeColorScheme);
}

void PreferencesWindow::on_buttonBox_clicked(QAbstractButton *button)
{
    if (buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole || buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole)
    {
        LOG("Saving settings from config window.")
        SaveCurrentConfig();
        emit SettingsChanged();
        if (NeedRestart)
        {
            QvMessageBoxWarn(this, tr("Some settings requires restart."), tr("Qv2ray will restart to take effect immediately."));
            QvCoreApplication->RestartApplication();
        }
    }
}

void PreferencesWindow::on_quietModeCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.uiConfig.quietMode = arg1 == Qt::Unchecked;
}

void PreferencesWindow::on_qvProxyNoProxy_clicked()
{
    LOADINGCHECK
    CurrentConfig.networkConfig.proxyType = Qv2rayConfig_Network::QVPROXY_NONE;
    SET_PROXY_UI_ENABLE(false)
}

void PreferencesWindow::on_qvProxySystemProxy_clicked()
{
    LOADINGCHECK
    CurrentConfig.networkConfig.proxyType = Qv2rayConfig_Network::QVPROXY_SYSTEM;
    SET_PROXY_UI_ENABLE(false)
}

void PreferencesWindow::on_qvProxyCustomProxy_clicked()
{
    LOADINGCHECK
    CurrentConfig.networkConfig.proxyType = Qv2rayConfig_Network::QVPROXY_CUSTOM;
    SET_PROXY_UI_ENABLE(true)
}

void PreferencesWindow::on_systemProxyTypeComboBox_currentIndexChanged(int index)
{
    LOADINGCHECK
    CurrentConfig.inboundConfig.systemProxySettings.proxyType = Qv2rayConfig_SystemProxy::ProxyType(systemProxyTypeComboBox->itemData(index).toInt());
}

void PreferencesWindow::on_socksGroupBox_clicked(bool checked)
{
    LOADINGCHECK
    CurrentConfig.inboundConfig.useSocks = checked;
    socksAuthCB->setEnabled(checked);
    socksUDPCB->setEnabled(checked);
    socksAuthUsernameTxt->setEnabled(checked && socksAuthCB->isChecked());
    socksAuthPasswordTxt->setEnabled(checked && socksAuthCB->isChecked());
    socksUDPIP->setEnabled(checked && socksUDPCB->isChecked());
    socksSniffingMetadataOnly->setEnabled(checked && socksSniffingCB->isChecked());
    socksOverrideHTTPCB->setEnabled(checked && socksSniffingCB->isChecked());
    socksOverrideTLSCB->setEnabled(checked && socksSniffingCB->isChecked());
    socksOverrideFakeDNSCB->setEnabled(checked && socksSniffingCB->isChecked());
    socksOverrideFakeDNSOthersCB->setEnabled(checked && socksSniffingCB->isChecked());
}

void PreferencesWindow::on_socksAuthCB_clicked(bool checked)
{
    LOADINGCHECK
    socksAuthUsernameTxt->setEnabled(checked);
    socksAuthPasswordTxt->setEnabled(checked);
}

void PreferencesWindow::on_socksUDPCB_clicked(bool checked)
{
    LOADINGCHECK
    socksUDPIP->setEnabled(checked);
}

void PreferencesWindow::on_socksSniffingCB_clicked(bool checked)
{
    LOADINGCHECK
    socksSniffingMetadataOnly->setEnabled(checked);
    socksOverrideHTTPCB->setEnabled(checked);
    socksOverrideTLSCB->setEnabled(checked);
    socksOverrideFakeDNSCB->setEnabled(checked);
    socksOverrideFakeDNSOthersCB->setEnabled(checked);
}

void PreferencesWindow::on_httpGroupBox_clicked(bool checked)
{
    LOADINGCHECK
    CurrentConfig.inboundConfig.useHTTP = checked;
    httpAuthCB->setEnabled(checked);
    httpAuthUsernameTxt->setEnabled(checked && httpAuthCB->isChecked());
    httpAuthPasswordTxt->setEnabled(checked && httpAuthCB->isChecked());
    httpSniffingMetadataOnly->setEnabled(checked && httpSniffingCB->isChecked());
    httpOverrideHTTPCB->setEnabled(checked && httpSniffingCB->isChecked());
    httpOverrideTLSCB->setEnabled(checked && httpSniffingCB->isChecked());
    httpOverrideFakeDNSCB->setEnabled(checked && httpSniffingCB->isChecked());
    httpOverrideFakeDNSOthersCB->setEnabled(checked && httpSniffingCB->isChecked());
}

void PreferencesWindow::on_httpAuthCB_clicked(bool checked)
{
    LOADINGCHECK
    httpAuthUsernameTxt->setEnabled(checked);
    httpAuthPasswordTxt->setEnabled(checked);
}

void PreferencesWindow::on_httpSniffingCB_clicked(bool checked)
{
    LOADINGCHECK
    httpSniffingMetadataOnly->setEnabled(checked);
    httpOverrideHTTPCB->setEnabled(checked);
    httpOverrideTLSCB->setEnabled(checked);
    httpOverrideFakeDNSCB->setEnabled(checked);
    httpOverrideFakeDNSOthersCB->setEnabled(checked);
}

void PreferencesWindow::on_tproxyGroupBox_clicked(bool checked)
{
    LOADINGCHECK
    CurrentConfig.inboundConfig.useTPROXY = checked;
    tproxyEnableTCP->setEnabled(checked);
    tproxyEnableUDP->setEnabled(checked);
    tproxySniffingMetadataOnlyCB->setEnabled(checked && tproxySniffingCB->isChecked());
    tproxyOverrideHTTPCB->setEnabled(checked && tproxySniffingCB->isChecked());
    tproxyOverrideTLSCB->setEnabled(checked && tproxySniffingCB->isChecked());
    tproxyOverrideFakeDNSCB->setEnabled(checked && tproxySniffingCB->isChecked());
    tproxyOverrideFakeDNSOthersCB->setEnabled(checked && tproxySniffingCB->isChecked());
}

void PreferencesWindow::on_tproxySniffingCB_clicked(bool checked)
{
    LOADINGCHECK
    tproxySniffingMetadataOnlyCB->setEnabled(checked);
    tproxyOverrideHTTPCB->setEnabled(checked);
    tproxyOverrideTLSCB->setEnabled(checked);
    tproxyOverrideFakeDNSCB->setEnabled(checked);
    tproxyOverrideFakeDNSOthersCB->setEnabled(checked);
}

void PreferencesWindow::on_setTestLatencyOnConnectedCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.advancedConfig.testLatencyOnConnected = arg1 == Qt::Checked;
}

void PreferencesWindow::on_setTestLatencyCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.advancedConfig.testLatencyPeriodically = arg1 == Qt::Checked;
}

void PreferencesWindow::on_disableSystemRootCB_stateChanged(int arg1)
{
    LOADINGCHECK
    CurrentConfig.advancedConfig.disableSystemRoot = arg1 == Qt::Checked;
}

void PreferencesWindow::on_buttonBox_accepted()
{
}
