#include "QvPluginHost.hpp"

#include "base/Qv2rayBase.hpp"
#include "base/Qv2rayLog.hpp"
#include "core/settings/SettingsBackend.hpp"
#include "plugins/protocols/BuiltinProtocolPlugin.hpp"
#include "plugins/subscription-adapters/BuiltinSubscriptionAdapter.hpp"
#include "utils/QvHelpers.hpp"

#define QV_MODULE_NAME "PluginHost"
namespace Qv2ray::components::plugins
{
    namespace
    {
        constexpr auto ProtocolComponentName = "qvplugin_builtin_protocol";
        constexpr auto SubscriptionComponentName = "builtin_subscription_support";
    }

    QvPluginHost::QvPluginHost(QObject *parent) : QObject(parent)
    {
        if (auto dir = QDir(QV2RAY_PLUGIN_SETTINGS_DIR); !dir.exists())
            dir.mkpath(QV2RAY_PLUGIN_SETTINGS_DIR);
        initializePluginHost();
    }

    int QvPluginHost::refreshPluginList()
    {
        clearPlugins();
        LOG("Registering built-in Qv2ray-Z components");

        const auto registerComponent = [this](QObject *componentObject, Qv2rayInterface *componentInterface, const QString &expectedInternalName) {
            if (componentObject == nullptr || componentInterface == nullptr)
                return false;

            componentObject->setParent(this);
            QvPluginInfo info;
            info.componentObject = componentObject;
            info.pluginInterface = componentInterface;
            info.metadata = componentInterface->GetMetadata();

            if (info.metadata.InternalName != expectedInternalName)
            {
                LOG("Internal component identity mismatch: expected " + expectedInternalName + ", got " + info.metadata.InternalName);
                delete componentObject;
                return false;
            }
            if (plugins.contains(info.metadata.InternalName))
            {
                LOG("Internal component was already registered: " + info.metadata.InternalName);
                delete componentObject;
                return false;
            }

            connect(componentObject, SIGNAL(PluginLog(const QString &)), this, SLOT(QvPluginLog(const QString &)));
            connect(componentObject, SIGNAL(PluginErrorMessageBox(const QString &, const QString &)), this,
                    SLOT(QvPluginMessageBox(const QString &, const QString &)));
            LOG("Registered internal component: \"" + info.metadata.Name + "\"");
            plugins.insert(info.metadata.InternalName, info);
            return true;
        };

        auto protocolComponent = new InternalProtocolSupportPlugin();
        registerComponent(protocolComponent, static_cast<Qv2rayInterface *>(protocolComponent), ProtocolComponentName);

        auto subscriptionComponent = new InternalSubscriptionSupportPlugin();
        registerComponent(subscriptionComponent, static_cast<Qv2rayInterface *>(subscriptionComponent), SubscriptionComponentName);

        return plugins.count();
    }

    void QvPluginHost::QvPluginLog(const QString &log)
    {
        const auto source = sender();
        for (const auto &plugin : plugins)
        {
            if (plugin.componentObject == source)
            {
                LOG(plugin.metadata.InternalName, log);
                return;
            }
        }
        LOG("UNKNOWN INTERNAL COMPONENT: " + log);
    }

    void QvPluginHost::QvPluginMessageBox(const QString &title, const QString &message)
    {
        const auto source = sender();
        for (const auto &plugin : plugins)
        {
            if (plugin.componentObject == source)
            {
                QvMessageBoxWarn(nullptr, plugin.metadata.Name + " - " + title, message);
                return;
            }
        }
        QvMessageBoxWarn(nullptr, "Unknown Internal Component - " + title, message);
    }

    void QvPluginHost::initializePluginHost()
    {
        refreshPluginList();
        for (const auto &plugin : plugins.keys())
            initializePlugin(plugin);
    }

    void QvPluginHost::clearPlugins()
    {
        const auto names = plugins.keys();
        for (const auto &name : names)
        {
            auto &plugin = plugins[name];
            DEBUG("Destroying internal component: \"" + plugin.metadata.Name + "\"");
            delete plugin.componentObject;
            plugin.componentObject = nullptr;
            plugin.pluginInterface = nullptr;
        }
        plugins.clear();
    }

    bool QvPluginHost::initializePlugin(const QString &internalName)
    {
        if (!plugins.contains(internalName))
        {
            LOG("Refusing to initialize an unknown internal component: " + internalName);
            return false;
        }

        auto &plugin = plugins[internalName];
        if (plugin.isLoaded)
        {
            LOG("The internal component \"" + internalName + "\" has already been initialized.");
            return true;
        }

        const auto conf = JsonFromString(StringFromFile(QV2RAY_PLUGIN_SETTINGS_DIR + internalName + ".conf"));
        if (!plugin.pluginInterface->InitializePlugin(QV2RAY_PLUGIN_SETTINGS_DIR + internalName + "/", conf))
        {
            LOG("Internal component initialization failed: " + internalName);
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
                LOG("Saving internal component settings for: \"" + name + "\"");
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
                    thisPluginCanHandle = thisPluginCanHandle || sharelink.startsWith(prefix);
                if (thisPluginCanHandle)
                {
                    auto opt = plugin.componentObject->property(QV2RAY_PLUGIN_INTERNAL_PROPERTY_KEY).value<Qv2rayPluginOption>();
                    opt[OPTION_SET_TLS_DISABLE_SYSTEM_CERTS] = GlobalConfig.advancedConfig.disableSystemRoot;
                    plugin.componentObject->setProperty(QV2RAY_PLUGIN_INTERNAL_PROPERTY_KEY, QVariant::fromValue(opt));
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
                    serializer->SetOutboundInfo(protocol, info, o);
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
} // namespace Qv2ray::components::plugins
