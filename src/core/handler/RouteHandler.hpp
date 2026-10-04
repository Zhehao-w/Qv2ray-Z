#pragma once

#include "base/Qv2rayBase.hpp"
#include "core/handler/RouteStorage.hpp"

namespace Qv2ray::core::handler
{
    class RouteHandler : public QObject
    {
        Q_OBJECT
      public:
        explicit RouteHandler(QObject *parent = nullptr);
        ~RouteHandler();
        bool SaveRoutes();
        route_storage::RouteStorageState GetRouteStorageState() const
        {
            return routeStorageState;
        }
        QString GetRouteStorageError() const
        {
            return routeStorageError;
        }
        //
        std::tuple<bool, QvConfig_DNS, QvConfig_FakeDNS> GetDNSSettings(const GroupRoutingId &id) const
        {
            return { configs[id].overrideDNS, configs[id].dnsConfig, configs[id].fakeDNSConfig };
        }
        std::pair<bool, QvConfig_Route> GetAdvancedRoutingSettings(const GroupRoutingId &id) const
        {
            return { configs[id].overrideRoute, configs[id].routeConfig };
        }
        //
        bool SetDNSSettings(const GroupRoutingId &id, bool overrideGlobal, const QvConfig_DNS &dns, const QvConfig_FakeDNS &fakeDNS);
        bool SetAdvancedRouteSettings(const GroupRoutingId &id, bool overrideGlobal, const QvConfig_Route &dns);
        //
        OUTBOUNDS ExpandExternalConnection(const OUTBOUNDS &outbounds) const;
        //
        // Final Config Generation
        CONFIGROOT GenerateFinalConfig(const ConnectionGroupPair &pair, bool hasAPI = true) const;
        CONFIGROOT GenerateFinalConfig(CONFIGROOT root, const GroupRoutingId &routingId, bool hasAPI = true) const;
        //
        bool ExpandChainedOutbounds(CONFIGROOT &) const;

      private:
        bool CanMutateRoutes() const
        {
            return route_storage::IsRouteStorageWritable(routeStorageState);
        }

        QHash<GroupRoutingId, GroupRoutingConfig> configs;
        QJsonObject routeStorageObject;
        route_storage::RouteStorageState routeStorageState = route_storage::RouteStorageState::Missing;
        QString routeStorageError;
        bool routeStorageDirty = false;
    };
    inline ::Qv2ray::core::handler::RouteHandler *RouteManager = nullptr;
} // namespace Qv2ray::core::handler
