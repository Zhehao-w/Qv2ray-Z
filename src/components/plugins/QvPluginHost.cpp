#include "QvPluginHost.hpp"

#include "BundledPluginPolicy.hpp"
#include "base/Qv2rayBase.hpp"
#include "base/Qv2rayLog.hpp"
#include "core/settings/SettingsBackend.hpp"
#include "utils/QvHelpers.hpp"

#include <QFileInfo>
#include <QPluginLoader>

#define QV_MODULE_NAME "PluginHost"
namespace Qv2ray::components::plugins
{
    using namespace policy;

    QvPluginHost::QvPluginHost(QObject *parent) : QObject(parent)
    {
        if (QvCoreApplication->StartupArguments.noPlugins)
        {
            LOG("The legacy --no-plugins option is deprecated and ignored; bundled components are required by Qv2ray-Z.");
        }
        if (!GlobalConfig.pluginConfig.pluginStates.isEmpty())
        {
            LOG("Legacy plugin enable-state configuration is ignored; external plugins are no longer supported.");
        }
        if (auto dir = QDir(QV2RAY_PLUGIN_SETTINGS_DIR); !dir.exists())
        {
            dir.mkpath(QV2RAY_PLUGIN_SETTINGS_DIR);
        }
        initializePluginHost();
    }

    int QvPluginHost::refreshPluginList()
    {
        clearPlugins();
        LOG("Loading bundled Qv2ray-Z components");

        const auto pluginDirectories = BundledPluginDirectories(QCoreApplication::applicationDirPath());
        for (const auto &spec : BundledPluginSpecs())
        {
            bool loaded = false;
            for (const auto &pluginDirPath : pluginDirectories)
            {
                const auto pluginFullPath = QDir(pluginDirPath).absoluteFilePath(spec.fileName);
                if (!QFileInfo(pluginFullPath).isFile())
                    continue;

                DEBUG("Loading bundled component: " + spec.fileName + " from: " + pluginDirPath);
                QvPluginInfo info;
                info.libraryPath = pluginFullPath;
                info.pluginLoader = new QPluginLoader(pluginFullPath, this);

                QObject *plugin = info.pluginLoader->instance();
                if (plugin == nullptr)
                {
                    LOG("Failed to load bundled component " + spec.fileName + ": " + info.pluginLoader->errorString());
                    info.pluginLoader->deleteLater();
                    continue;
                }

                info.pluginInterface = qobject_cast<Qv2rayInterface *>(plugin);
                if (info.pluginInterface == nullptr)
                {
                    LOG("Bundled component does not implement the Qv2ray plugin interface: " + spec.fileName);
                    info.pluginLoader->unload();
                    info.pluginLoader->deleteLater();
                    continue;
                }

                if (info.pluginInterface->QvPluginInterfaceVersion != QV2RAY_PLUGIN_INTERFACE_VERSION)
                {
                    LOG("Bundled component has an incompatible interface version: " + spec.fileName);
                    QvMessageBoxWarn(nullptr, tr("Cannot load bundled component"),
                                     tr("A bundled Qv2ray-Z component was built against an incompatible interface version. Please reinstall Qv2ray-Z."));
                    info.pluginLoader->unload();
                    info.pluginLoader->deleteLater();
                    continue;
                }

                info.metadata = info.pluginInterface->GetMetadata();
                if (!BundledPluginIdentityMatches(spec.fileName, info.metadata.InternalName))
                {
                    LOG("Bundled component identity mismatch; refusing to load: " + spec.fileName);
                    info.pluginLoader->unload();
                    info.pluginLoader->deleteLater();
                    continue;
                }
                if (plugins.contains(info.metadata.InternalName))
                {
                    LOG("Bundled component was already loaded: " + info.metadata.InternalName);
                    info.pluginLoader->unload();
                    info.pluginLoader->deleteLater();
                    continue;
                }

                connect(plugin, SIGNAL(PluginLog(const QString &)), this, SLOT(QvPluginLog(const QString &)));
                connect(plugin, SIGNAL(PluginErrorMessageBox(const QString &, const QString &)), this,
                        SLOT(QvPluginMessageBox(const QString &, const QString &)));
                LOG("Loaded bundled component: \"" + info.metadata.Name + "\"");
                plugins.insert(info.metadata.InternalName, info);
                loaded = true;
                break;
            }

            if (!loaded)
                LOG("Bundled component not found or could not be loaded: " + spec.internalName);
        }
        return plugins.count();
    }

    void QvPluginHost::QvPluginLog(const QString &log)
    {
        auto _sender = sender();
        if (auto _interface = qobject_cast<Qv2rayInterface *>(_sender); _interface)
        {
            LOG(_interface->GetMetadata().InternalName, log);
        }
        else
        {
            LOG("UNKNOWN CLIENT: " + log);
        }
    }

    void QvPluginHost::QvPluginMessageBox(const QString &title, const QString &message)
    {
        const auto _sender = sender();
        const auto _interface = qobject_cast<Qv2rayInterface *>(_sender);
        if (_interface)
            QvMessageBoxWarn(nullptr, _interface->GetMetadata().Name + " - " + title, message);
        else
            QvMessageBoxWarn(nullptr, "Unknown Plugin - " + title, message);
    }

    bool QvPluginHost::GetPluginEnabled(const QString &internalName) const
    {
        return IsBundledPluginInternalName(internalName);
    }

    void QvPluginHost::SetPluginEnabled(const QString &internalName, bool isEnabled)
    {
        Q_UNUSED(isEnabled)
        if (IsBundledPluginInternalName(internalName))
            LOG("Bundled component enable state is fixed; ignoring state change for: " + internalName);
        else
            LOG("External plugin state change ignored because external plugins are no longer supported: " + internalName);
    }

    void QvPluginHost::initializePluginHost()
    {
        refreshPluginList();
        for (const auto &plugin : plugins.keys())
        {
            initializePlugin(plugin);
        }
    }

    void QvPluginHost::clearPlugins()
    {
        for (auto &&plugin : plugins)
        {
            DEBUG("Unloading bundled component: \"" + plugin.metadata.Name + "\"");
            plugin.pluginLoader->unload();
            plugin.pluginLoader->deleteLater();
        }
        plugins.clear();
    }

    bool QvPluginHost::initializePlugin(const QString &internalName)
    {
        if (!plugins.contains(internalName) || !IsBundledPluginInternalName(internalName))
        {
            LOG("Refusing to initialize a non-bundled plugin: " + internalName);
            return false;
        }

        auto &plugin = plugins[internalName];
        if (plugin.isLoaded)
        {
            LOG("The bundled component \"" + internalName + "\" has already been initialized.");
            return true;
        }

        const auto conf = JsonFromString(StringFromFile(QV2RAY_PLUGIN_SETTINGS_DIR + internalName + ".conf"));
        if (!plugin.pluginInterface->InitializePlugin(QV2RAY_PLUGIN_SETTINGS_DIR + internalName + "/", conf))
        {
            LOG("Bundled component initialization failed: " + internalName);
            return false;
        }
        plugin.isLoaded = true;
        return true;
    }

    void QvPluginHost::SavePluginSettings() const
    {
        for (const auto &name : plugins.keys())
        {
            if (plugins[name].isLoaded)
            {
                LOG("Saving bundled component settings for: \"" + name + "\"");
                auto &conf = plugins[name].pluginInterface->GetSettngs();
                StringToFile(JsonToString(conf), QV2RAY_PLUGIN_SETTINGS_DIR + name + ".conf");
            }
        }
    }

    QvPluginHost::~QvPluginHost()
    {
        SavePluginSettings();
        clearPlugins();
    }

    // ================== BEGIN SEND EVENTS ==================
    void QvPluginHost::SendEvent(const Events::ConnectionStats::EventObject &object)
    {
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_EVENT_HANDLER))
                plugin.pluginInterface->GetEventHandler()->ProcessEvent_ConnectionStats(object);
        }
    }
    void QvPluginHost::SendEvent(const Events::Connectivity::EventObject &object)
    {
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_EVENT_HANDLER))
                plugin.pluginInterface->GetEventHandler()->ProcessEvent_Connectivity(object);
        }
    }
    void QvPluginHost::SendEvent(const Events::ConnectionEntry::EventObject &object)
    {
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_EVENT_HANDLER))
                plugin.pluginInterface->GetEventHandler()->ProcessEvent_ConnectionEntry(object);
        }
    }
    void QvPluginHost::SendEvent(const Events::SystemProxy::EventObject &object)
    {
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_EVENT_HANDLER))
                plugin.pluginInterface->GetEventHandler()->ProcessEvent_SystemProxy(object);
        }
    }

    const QList<std::tuple<QString, QString, QJsonObject>> QvPluginHost::TryDeserializeShareLink(const QString &sharelink, //
                                                                                                 QString *aliasPrefix,     //
                                                                                                 QString *errMessage,      //
                                                                                                 QString *newGroupName,    //
                                                                                                 bool &ok) const
    {
        Q_UNUSED(newGroupName)
        QList<std::tuple<QString, QString, QJsonObject>> data;
        ok = false;
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_OUTBOUND_HANDLER))
            {
                auto serializer = plugin.pluginInterface->GetOutboundHandler();
                bool thisPluginCanHandle = false;
                for (const auto &prefix : serializer->SupportedLinkPrefixes())
                {
                    thisPluginCanHandle = thisPluginCanHandle || sharelink.startsWith(prefix);
                }
                if (thisPluginCanHandle)
                {
                    auto opt = plugin.pluginLoader->instance()->property(QV2RAY_PLUGIN_INTERNAL_PROPERTY_KEY).value<Qv2rayPluginOption>();
                    opt[OPTION_SET_TLS_DISABLE_SYSTEM_CERTS] = GlobalConfig.advancedConfig.disableSystemRoot;
                    plugin.pluginLoader->instance()->setProperty(QV2RAY_PLUGIN_INTERNAL_PROPERTY_KEY, QVariant::fromValue(opt));
                    const auto &[protocol, outboundSettings] = serializer->DeserializeOutbound(sharelink, aliasPrefix, errMessage);
                    if (errMessage->isEmpty())
                    {
                        data << std::tuple{ *aliasPrefix, protocol, outboundSettings };
                        ok = true;
                    }
                    break;
                }
            }
        }
        return data;
    }

    const OutboundInfoObject QvPluginHost::GetOutboundInfo(const QString &protocol, const QJsonObject &o, bool &status) const
    {
        status = false;
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_OUTBOUND_HANDLER))
            {
                auto serializer = plugin.pluginInterface->GetOutboundHandler();
                if (serializer && serializer->SupportedProtocols().contains(protocol))
                {
                    auto info = serializer->GetOutboundInfo(protocol, o);
                    status = true;
                    return info;
                }
            }
        }
        return {};
    }

    void QvPluginHost::SetOutboundInfo(const QString &protocol, const OutboundInfoObject &info, QJsonObject &o) const
    {
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_OUTBOUND_HANDLER))
            {
                auto serializer = plugin.pluginInterface->GetOutboundHandler();
                if (serializer && serializer->SupportedProtocols().contains(protocol))
                {
                    serializer->SetOutboundInfo(protocol, info, o);
                }
            }
        }
    }

    const QString QvPluginHost::SerializeOutbound(const QString &protocol,           //
                                                  const QJsonObject &out,            //
                                                  const QJsonObject &streamSettings, //
                                                  const QString &name,               //
                                                  const QString &group,              //
                                                  bool *ok) const
    {
        *ok = false;
        for (const auto &plugin : plugins)
        {
            if (plugin.isLoaded && plugin.metadata.Components.contains(COMPONENT_OUTBOUND_HANDLER))
            {
                auto serializer = plugin.pluginInterface->GetOutboundHandler();
                if (serializer && serializer->SupportedProtocols().contains(protocol))
                {
                    auto link = serializer->SerializeOutbound(protocol, name, group, out, streamSettings);
                    *ok = true;
                    return link;
                }
            }
        }
        return "";
    }

    const QStringList GetPluginComponentsString(const QList<PluginGuiComponentType> &types)
    {
        QStringList typesList;
        if (types.isEmpty())
            typesList << QObject::tr("None");
        for (auto type : types)
        {
            switch (type)
            {
                case GUI_COMPONENT_SETTINGS: typesList << QObject::tr("Settings Widget"); break;
                case GUI_COMPONENT_INBOUND_EDITOR: typesList << QObject::tr("Inbound Editor"); break;
                case GUI_COMPONENT_OUTBOUND_EDITOR: typesList << QObject::tr("Outbound Editor"); break;
                case GUI_COMPONENT_MAINWINDOW_WIDGET: typesList << QObject::tr("MainWindow Widget"); break;
                default: typesList << QObject::tr("Unknown type."); break;
            }
        }
        return typesList;
    }

    const QStringList GetPluginComponentsString(const QList<PluginComponentType> &types)
    {
        QStringList typesList;
        if (types.isEmpty())
            typesList << QObject::tr("None");
        for (auto type : types)
        {
            switch (type)
            {
                case COMPONENT_KERNEL: typesList << QObject::tr("Kernel"); break;
                case COMPONENT_OUTBOUND_HANDLER: typesList << QObject::tr("Outbound Handler/Parser"); break;
                case COMPONENT_SUBSCRIPTION_ADAPTER: typesList << QObject::tr("Subscription Adapter"); break;
                case COMPONENT_EVENT_HANDLER: typesList << QObject::tr("Event Handler"); break;
                case COMPONENT_GUI: typesList << QObject::tr("GUI Components"); break;
                default: typesList << QObject::tr("Unknown type."); break;
            }
        }
        return typesList;
    }
} // namespace Qv2ray::components::plugins
