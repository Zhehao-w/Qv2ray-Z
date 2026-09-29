#include "ConfigHandler.hpp"

#include "ConfigDataSafety.hpp"
#include "components/plugins/QvPluginHost.hpp"
#include "core/connection/Serialization.hpp"
#include "core/handler/RouteHandler.hpp"
#include "core/settings/SettingsBackend.hpp"
#include "utils/HTTPRequestHelper.hpp"
#include "utils/QvHelpers.hpp"

#define QV_MODULE_NAME "ConfigHandler"

namespace Qv2ray::core::handler
{
    QvConfigHandler::QvConfigHandler(QObject *parent) : QObject(parent)
    {
        DEBUG("ConnectionHandler Constructor.");

        const auto connectionsPath = QV2RAY_CONFIG_DIR + "connections.json";
        const auto groupsPath = QV2RAY_CONFIG_DIR + "groups.json";
        const auto connectionFile = ReadJsonObjectFile(connectionsPath);
        const auto groupFile = ReadJsonObjectFile(groupsPath);
        const bool bothMissing = connectionFile.status == JsonObjectFileStatus::Missing && groupFile.status == JsonObjectFileStatus::Missing;
        const bool bothValid = connectionFile.status == JsonObjectFileStatus::Valid && groupFile.status == JsonObjectFileStatus::Valid;
        metadataPersistenceEnabled = bothMissing || bothValid;

        const auto connectionJson = connectionFile.status == JsonObjectFileStatus::Valid ? connectionFile.object : QJsonObject{};
        const auto groupJson = groupFile.status == JsonObjectFileStatus::Valid ? groupFile.object : QJsonObject{};

        if (!metadataPersistenceEnabled)
        {
            LOG("Connection metadata is incomplete or invalid; destructive recovery and metadata persistence are disabled for this session.");
            if (connectionFile.status == JsonObjectFileStatus::Invalid)
                LOG("connections.json error: " + connectionFile.error);
            if (groupFile.status == JsonObjectFileStatus::Invalid)
                LOG("groups.json error: " + groupFile.error);
        }

        for (const auto &connectionId : connectionJson.keys())
        {
            connections.insert(ConnectionId{ connectionId }, ConnectionObject::fromJson(connectionJson.value(connectionId).toObject()));
        }

        for (const auto &groupId : groupJson.keys())
        {
            auto groupObject = GroupObject::fromJson(groupJson.value(groupId).toObject());
            if (groupObject.displayName.isEmpty())
            {
                groupObject.displayName = tr("Group: %1").arg(GenerateRandomString(5));
            }
            groups.insert(GroupId{ groupId }, groupObject);
            for (const auto &connId : groupObject.connections)
            {
                if (connections.contains(connId))
                {
                    connections[connId].__qvConnectionRefCount++;
                }
                else
                {
                    metadataPersistenceEnabled = false;
                    LOG("Group metadata references an unknown connection id: " + connId.toString());
                }
            }
        }

        // Always provide an in-memory recovery target. When metadata is unsafe we
        // intentionally do not persist this reconstructed view over the original files.
        if (!groups.contains(DefaultGroupId))
        {
            groups.insert(DefaultGroupId, {});
            groups[DefaultGroupId].displayName = tr("Default Group");
            groups[DefaultGroupId].isSubscription = false;
        }

        for (const auto &id : connections.keys())
        {
            const auto connectionFilePath = QV2RAY_CONNECTIONS_DIR + id.toString() + QV2RAY_CONFIG_FILE_EXTENSION;
            const auto rootFile = ReadJsonObjectFile(connectionFilePath);
            if (rootFile.status == JsonObjectFileStatus::Valid)
            {
                connectionRootCache[id] = CONFIGROOT(rootFile.object);
                DEBUG("Loaded connection id: " + id.toString() + " into cache.");
            }
            else
            {
                metadataPersistenceEnabled = false;
                LOG("Connection config is missing or invalid for id: " + id.toString() +
                    (rootFile.error.isEmpty() ? QString() : ", error: " + rootFile.error));
            }

            if (connections[id].__qvConnectionRefCount == 0)
            {
                if (!groups[DefaultGroupId].connections.contains(id))
                    groups[DefaultGroupId].connections.append(id);
                connections[id].__qvConnectionRefCount = 1;
                LOG("Recovered orphan connection into Default Group instead of deleting it: " + id.toString());
            }
        }

        if (!metadataPersistenceEnabled)
        {
            QvMessageBoxWarn(nullptr, tr("Connection data needs recovery"),
                             tr("Qv2ray-Z found incomplete or corrupted connection metadata. Recoverable entries were kept in memory and no connection files were deleted. ") +
                                 tr("To avoid overwriting recoverable data, connection/group metadata will remain read-only for this session. ") +
                                 tr("Restore or repair connections.json, groups.json, and any reported connection files before making persistent changes."));
        }

        kernelHandler = new KernelInstanceHandler(this);
        connect(kernelHandler, &KernelInstanceHandler::OnCrashed, this, &QvConfigHandler::p_OnKernelCrashed);
        connect(kernelHandler, &KernelInstanceHandler::OnStatsDataAvailable, this, &QvConfigHandler::p_OnStatsDataArrived);
        connect(kernelHandler, &KernelInstanceHandler::OnKernelLogAvailable, this, &QvConfigHandler::OnKernelLogAvailable);
        connect(kernelHandler, &KernelInstanceHandler::OnConnected, this, &QvConfigHandler::OnConnected);
        connect(kernelHandler, &KernelInstanceHandler::OnDisconnected, this, &QvConfigHandler::OnDisconnected);
        //
        pingHelper = new LatencyTestHost(5, this);
        connect(pingHelper, &LatencyTestHost::OnLatencyTestCompleted, this, &QvConfigHandler::p_OnLatencyDataArrived);
        //
        // Save per 1 hour.
        saveTimerId = startTimer(1 * 60 * 60 * 1000);
        // Do not ping all...
        pingConnectionTimerId = startTimer(60 * 1000);
    }

    bool QvConfigHandler::SaveConnectionConfig()
    {
        if (!metadataPersistenceEnabled)
        {
            LOG("Refusing to overwrite connection metadata while the loaded metadata set is incomplete or corrupted.");
            return false;
        }

        QJsonObject connectionsObject;
        for (const auto &key : connections.keys())
        {
            connectionsObject[key.toString()] = connections[key].toJson();
        }

        QJsonObject groupObject;
        for (const auto &key : groups.keys())
        {
            groupObject[key.toString()] = groups[key].toJson();
        }

        const auto connectionsPath = QV2RAY_CONFIG_DIR + "connections.json";
        const auto groupsPath = QV2RAY_CONFIG_DIR + "groups.json";
        const bool connectionsExisted = QFile::exists(connectionsPath);
        const auto oldConnections = connectionsExisted ? StringFromFile(connectionsPath) : QString{};

        if (!StringToFile(JsonToString(connectionsObject), connectionsPath))
        {
            LOG("Failed to save connections.json.");
            return false;
        }

        if (!StringToFile(JsonToString(groupObject), groupsPath))
        {
            LOG("Failed to save groups.json; rolling back connections.json.");
            const bool rolledBack = connectionsExisted ? StringToFile(oldConnections, connectionsPath) : QFile::remove(connectionsPath);
            if (!rolledBack && (connectionsExisted || QFile::exists(connectionsPath)))
                LOG("CRITICAL: failed to roll back connections.json after groups.json write failure.");
            return false;
        }

        return true;
    }

    void QvConfigHandler::timerEvent(QTimerEvent *event)
    {
        if (event->timerId() == saveTimerId)
        {
            SaveConnectionConfig();
        }
        else if (event->timerId() == pingAllTimerId)
        {
            StartLatencyTest();
        }
        else if (event->timerId() == pingConnectionTimerId)
        {
            auto id = kernelHandler->CurrentConnection();
            if (!id.isEmpty() && GlobalConfig.advancedConfig.testLatencyPeriodically)
            {
                StartLatencyTest(id.connectionId, GlobalConfig.networkConfig.latencyTestingMethod);
            }
        }
    }

    void QvConfigHandler::StartLatencyTest()
    {
        for (const auto &connection : connections.keys())
        {
            emit OnLatencyTestStarted(connection);
        }
        pingHelper->TestLatency(connections.keys(), GlobalConfig.networkConfig.latencyTestingMethod);
    }

    void QvConfigHandler::StartLatencyTest(const GroupId &id)
    {
        for (const auto &connection : groups[id].connections)
        {
            emit OnLatencyTestStarted(connection);
        }
        pingHelper->TestLatency(groups[id].connections, GlobalConfig.networkConfig.latencyTestingMethod);
    }

    void QvConfigHandler::StartLatencyTest(const ConnectionId &id, Qv2rayLatencyTestingMethod method)
    {
        emit OnLatencyTestStarted(id);
        pingHelper->TestLatency(id, method);
    }

    const QList<GroupId> QvConfigHandler::Subscriptions() const
    {
        QList<GroupId> subsList;

        for (const auto &group : groups)
        {
            if (group.isSubscription)
            {
                subsList.push_back(groups.key(group));
            }
        }

        return subsList;
    }

    void QvConfigHandler::ClearGroupUsage(const GroupId &id)
    {
        for (const auto &conn : groups[id].connections)
        {
            ClearConnectionUsage({ conn, id });
        }
    }
    void QvConfigHandler::ClearConnectionUsage(const ConnectionGroupPair &id)
    {
        CheckValidId(id.connectionId, nothing);
        connections[id.connectionId].stats.Clear();
        emit OnStatsAvailable(id, {});
        PluginHost->SendEvent({ GetDisplayName(id.connectionId), 0, 0, 0, 0 });
        return;
    }

    const QList<GroupId> QvConfigHandler::GetConnectionContainedIn(const ConnectionId &connId) const
    {
        CheckValidId(connId, {});
        QList<GroupId> grps;
        for (const auto &group : groups)
        {
            if (group.connections.contains(connId))
                grps.push_back(groups.key(group));
        }
        return grps;
    }

    const std::optional<QString> QvConfigHandler::RenameConnection(const ConnectionId &id, const QString &newName)
    {
        CheckValidId(id, {});
        const auto originalName = connections[id].displayName;
        connections[id].displayName = newName;
        if (!SaveConnectionConfig())
        {
            connections[id].displayName = originalName;
            return tr("Failed to save connection metadata.");
        }
        emit OnConnectionRenamed(id, originalName, newName);
        PluginHost->SendEvent({ Events::ConnectionEntry::Renamed, newName, originalName });
        return {};
    }

    bool QvConfigHandler::RemoveConnectionFromGroup(const ConnectionId &id, const GroupId &gid)
    {
        CheckValidId(id, false);
        CheckValidId(gid, false);
        LOG("Removing connection : " + id.toString());
        if (groups[gid].connections.contains(id))
        {
            auto removedEntries = groups[gid].connections.removeAll(id);
            if (removedEntries > 1)
            {
                LOG("Found same connection occured multiple times in a group.");
            }
            // Decrease reference count.
            connections[id].__qvConnectionRefCount -= removedEntries;
        }

        if (GlobalConfig.autoStartId == ConnectionGroupPair{ id, gid })
        {
            GlobalConfig.autoStartId.clear();
        }

        // Emit everything first then clear the connection map.
        PluginHost->SendEvent({ Events::ConnectionEntry::RemovedFromGroup, GetDisplayName(id), "" });
        emit OnConnectionRemovedFromGroup({ id, gid });

        if (connections[id].__qvConnectionRefCount <= 0)
        {
            LOG("Fully removing a connection from cache.");
            connectionRootCache.remove(id);
            //
            QFile connectionFile(QV2RAY_CONNECTIONS_DIR + id.toString() + QV2RAY_CONFIG_FILE_EXTENSION);
            if (connectionFile.exists())
            {
                if (!connectionFile.remove())
                    LOG("Failed to remove connection config file");
            }
            connections.remove(id);
        }
        return true;
    }

    bool QvConfigHandler::LinkConnectionWithGroup(const ConnectionId &id, const GroupId &newGroupId)
    {
        CheckValidId(id, false);
        CheckValidId(newGroupId, false);
        if (groups[newGroupId].connections.contains(id))
        {
            LOG("Connection not linked since " + id.toString() + " is already in the group " + newGroupId.toString());
            return false;
        }
        groups[newGroupId].connections.append(id);
        connections[id].__qvConnectionRefCount++;
        PluginHost->SendEvent({ Events::ConnectionEntry::LinkedWithGroup, connections[id].displayName, "" });
        emit OnConnectionLinkedWithGroup({ id, newGroupId });
        return true;
    }

    bool QvConfigHandler::MoveConnectionFromToGroup(const ConnectionId &id, const GroupId &sourceGid, const GroupId &targetGid)
    {
        CheckValidId(id, false);
        CheckValidId(targetGid, false);
        CheckValidId(sourceGid, false);
        //
        if (!groups[sourceGid].connections.contains(id))
        {
            LOG("Trying to move a connection away from a group it does not belong to.");
            return false;
        }
        if (groups[targetGid].connections.contains(id))
        {
            LOG("The connection: " + id.toString() + " has already been in the target group: " + targetGid.toString());
            const auto removedCount = groups[sourceGid].connections.removeAll(id);
            connections[id].__qvConnectionRefCount -= removedCount;
        }
        else
        {
            // If the target group does not contain this connection.
            const auto removedCount = groups[sourceGid].connections.removeAll(id);
            connections[id].__qvConnectionRefCount -= removedCount;
            //
            groups[targetGid].connections.append(id);
            connections[id].__qvConnectionRefCount++;
        }

        emit OnConnectionRemovedFromGroup({ id, sourceGid });
        emit OnConnectionLinkedWithGroup({ id, targetGid });

        return true;
    }

    const std::optional<QString> QvConfigHandler::DeleteGroup(const GroupId &id)
    {
        CheckValidId(id, tr("Group does not exist"));
        // Copy construct
        auto list = groups[id].connections;
        for (const auto &conn : list)
        {
            MoveConnectionFromToGroup(conn, id, DefaultGroupId);
        }

        PluginHost->SendEvent({ Events::ConnectionEntry::FullyRemoved, groups[id].displayName, "" });

        groups.remove(id);
        SaveConnectionConfig();
        emit OnGroupDeleted(id, list);
        if (id == DefaultGroupId)
        {
            groups[id].displayName = tr("Default Group");
        }
        return {};
    }

    bool QvConfigHandler::StartConnection(const ConnectionGroupPair &identifier)
    {
        CheckValidId(identifier, false);
        connections[identifier.connectionId].lastConnected = system_clock::to_time_t(system_clock::now());
        //
        CONFIGROOT root = GetConnectionRoot(identifier.connectionId);
        const auto fullConfig = RouteManager->GenerateFinalConfig(root, groups[identifier.groupId].routeConfigId);
        //
        auto errMsg = kernelHandler->StartConnection(identifier, fullConfig);
        if (errMsg)
        {
            QvMessageBoxWarn(nullptr, tr("Failed to start connection"), *errMsg);
            return false;
        }

        GlobalConfig.lastConnectedId = identifier;
        return true;
    }

    void QvConfigHandler::RestartConnection()
    {
        StopConnection();
        StartConnection(GlobalConfig.lastConnectedId);
    }

    void QvConfigHandler::StopConnection()
    {
        kernelHandler->StopConnection();
    }

    void QvConfigHandler::p_OnKernelCrashed(const ConnectionGroupPair &id, const QString &errMessage)
    {
        LOG("Kernel crashed: " + errMessage);
        emit OnDisconnected(id);
        PluginHost->SendEvent({ GetDisplayName(id.connectionId), QMap<QString, int>{}, Events::Connectivity::Disconnected });
        emit OnKernelCrashed(id, errMessage);
    }

    QvConfigHandler::~QvConfigHandler()
    {
        LOG("Triggering save settings from destructor");
        delete kernelHandler;
        SaveConnectionConfig();
    }

    const CONFIGROOT QvConfigHandler::GetConnectionRoot(const ConnectionId &id) const
    {
        CheckValidId(id, CONFIGROOT());
        return connectionRootCache.value(id);
    }

    void QvConfigHandler::p_OnLatencyDataArrived(const ConnectionId &id, const LatencyTestResult &result)
    {
        CheckValidId(id, nothing);
        connections[id].latency = result.avg;
        emit OnLatencyTestFinished(id, result.avg);
    }

    bool QvConfigHandler::UpdateConnection(const ConnectionId &id, const CONFIGROOT &root, bool skipRestart)
    {
        CheckValidId(id, false);
        const auto path = QV2RAY_CONNECTIONS_DIR + id.toString() + QV2RAY_CONFIG_FILE_EXTENSION;
        if (!StringToFile(JsonToString(root), path))
        {
            LOG("Failed to persist connection config: " + id.toString());
            return false;
        }

        connectionRootCache[id] = root;
        emit OnConnectionModified(id);
        PluginHost->SendEvent({ Events::ConnectionEntry::Edited, connections[id].displayName, "" });
        if (!skipRestart && kernelHandler->CurrentConnection().connectionId == id)
        {
            RestartConnection();
        }
        return true;
    }

    const GroupId QvConfigHandler::CreateGroup(const QString &displayName, bool isSubscription)
    {
        GroupId id(GenerateRandomString());
        groups[id].displayName = displayName;
        groups[id].isSubscription = isSubscription;
        groups[id].creationDate = system_clock::to_time_t(system_clock::now());
        PluginHost->SendEvent({ Events::ConnectionEntry::Created, displayName, "" });
        emit OnGroupCreated(id, displayName);
        SaveConnectionConfig();
        return id;
    }

    const GroupRoutingId QvConfigHandler::GetGroupRoutingId(const GroupId &id)
    {
        if (groups[id].routeConfigId == NullRoutingId)
        {
            groups[id].routeConfigId = GroupRoutingId{ GenerateRandomString() };
        }
        return groups[id].routeConfigId;
    }

    const std::optional<QString> QvConfigHandler::RenameGroup(const GroupId &id, const QString &newName)
    {
        CheckValidId(id, tr("Group does not exist"));
        OnGroupRenamed(id, groups[id].displayName, newName);
        PluginHost->SendEvent({ Events::ConnectionEntry::Renamed, newName, groups[id].displayName });
        groups[id].displayName = newName;
        return {};
    }

    bool QvConfigHandler::SetSubscriptionData(const GroupId &id, std::optional<bool> isSubscription, const std::optional<QString> &address,
                                              std::optional<float> updateInterval)
    {
        CheckValidId(id, false);

        if (isSubscription.has_value())
            groups[id].isSubscription = *isSubscription;

        if (address.has_value())
            groups[id].subscriptionOption.address = *address;

        if (updateInterval.has_value())
            groups[id].subscriptionOption.updateInterval = *updateInterval;

        return true;
    }

    bool QvConfigHandler::SetSubscriptionType(const GroupId &id, const QString &type)
    {
        CheckValidId(id, false);
        groups[id].subscriptionOption.type = type;
        return true;
    }

    bool QvConfigHandler::SetSubscriptionIncludeKeywords(const GroupId &id, const QStringList &keywords)
    {
        CheckValidId(id, false);
        groups[id].subscriptionOption.IncludeKeywords.clear();

        for (const auto &keyword : keywords)
        {
            if (!keyword.trimmed().isEmpty())
            {
                groups[id].subscriptionOption.IncludeKeywords.push_back(keyword);
            }
        }
        return true;
    }

    bool QvConfigHandler::SetSubscriptionIncludeRelation(const GroupId &id, SubscriptionFilterRelation relation)
    {
        CheckValidId(id, false);
        groups[id].subscriptionOption.IncludeRelation = relation;
        return true;
    }

    bool QvConfigHandler::SetSubscriptionExcludeKeywords(const GroupId &id, const QStringList &keywords)
    {
        CheckValidId(id, false);
        groups[id].subscriptionOption.ExcludeKeywords.clear();
        for (const auto &keyword : keywords)
        {
            if (!keyword.trimmed().isEmpty())
            {
                groups[id].subscriptionOption.ExcludeKeywords.push_back(keyword);
            }
        }
        return true;
    }

    bool QvConfigHandler::SetSubscriptionExcludeRelation(const GroupId &id, SubscriptionFilterRelation relation)
    {
        CheckValidId(id, false);
        groups[id].subscriptionOption.ExcludeRelation = relation;
        return true;
    }

    void QvConfigHandler::UpdateSubscriptionAsync(const GroupId &id)
    {
        CheckValidId(id, nothing);
        if (!groups[id].isSubscription)
            return;
        NetworkRequestHelper::AsyncHttpGet(groups[id].subscriptionOption.address, [=](const QByteArray &d) {
            p_CHUpdateSubscription(id, d);
            emit OnSubscriptionAsyncUpdateFinished(id);
        });
    }

    bool QvConfigHandler::UpdateSubscription(const GroupId &id)
    {
        if (!groups[id].isSubscription)
            return false;
        const auto data = NetworkRequestHelper::HttpGet(groups[id].subscriptionOption.address);
        return p_CHUpdateSubscription(id, data);
    }

    bool QvConfigHandler::p_CHUpdateSubscription(const GroupId &id, const QByteArray &data)
    {
        CheckValidId(id, false);
        if (!metadataPersistenceEnabled)
        {
            QvMessageBoxWarn(nullptr, tr("Cannot Update Subscription"),
                             tr("Connection metadata is in recovery/read-only mode. Repair the metadata files and restart Qv2ray-Z before updating subscriptions."));
            return false;
        }

        std::shared_ptr<SubscriptionDecoder> decoder;
        {
            const auto type = groups[id].subscriptionOption.type;
            for (const auto &plugin : PluginHost->UsablePlugins())
            {
                const auto pluginInfo = PluginHost->GetPlugin(plugin);
                if (pluginInfo->hasComponent(COMPONENT_SUBSCRIPTION_ADAPTER))
                {
                    const auto adapterInterface = pluginInfo->pluginInterface->GetSubscriptionAdapter();
                    for (const auto &[t, _] : adapterInterface->SupportedSubscriptionTypes())
                    {
                        if (t == type)
                            decoder = adapterInterface->GetSubscriptionDecoder(t);
                    }
                }
            }

            if (decoder == nullptr)
            {
                QvMessageBoxWarn(nullptr, tr("Cannot Update Subscription"),
                                 tr("Unknown subscription type: %1").arg(type) + NEWLINE + tr("A subscription plugin is missing?"));
                return false;
            }
        }

        const auto groupName = groups[id].displayName;
        const auto result = decoder->DecodeData(data);
        QList<std::pair<QString, CONFIGROOT>> newConnections;

        for (const auto &[name, json] : result.connections)
        {
            newConnections.append({ name, CONFIGROOT(json) });
        }
        for (const auto &link : result.links)
        {
            QString alias;
            QString errMessage;
            QString decodedGroupName = groupName;
            const auto connectionConfigMap = ConvertConfigFromString(link.trimmed(), &alias, &errMessage, &decodedGroupName);
            if (!errMessage.isEmpty())
                LOG("Error: ", errMessage);
            newConnections << connectionConfigMap;
        }

        if (newConnections.count() < 5)
        {
            LOG("Found a subscription with less than 5 connections.");
            if (QvMessageBoxAsk(
                    nullptr, tr("Update Subscription"),
                    tr("%n entrie(s) have been found from the subscription source, do you want to continue?", "", newConnections.count())) != Yes)
                return false;
        }

        decltype(newConnections) filteredConnections;
        for (const auto &config : newConnections)
        {
            const bool isIncludeOperationAND = groups[id].subscriptionOption.IncludeRelation == RELATION_AND;
            const bool isExcludeOperationOR = groups[id].subscriptionOption.ExcludeRelation == RELATION_OR;
            bool includeConfig = isIncludeOperationAND;
            {
                bool hasIncludeItemMatched = false;
                for (const auto &key : groups[id].subscriptionOption.IncludeKeywords)
                {
                    if (!key.trimmed().isEmpty())
                    {
                        hasIncludeItemMatched = true;
                        if (!isIncludeOperationAND == config.first.contains(key.trimmed()))
                        {
                            includeConfig = !isIncludeOperationAND;
                            break;
                        }
                    }
                }
                if (!hasIncludeItemMatched)
                    includeConfig = true;
            }
            if (includeConfig)
            {
                bool hasExcludeItemMatched = false;
                includeConfig = isExcludeOperationOR;
                for (const auto &key : groups[id].subscriptionOption.ExcludeKeywords)
                {
                    if (!key.trimmed().isEmpty())
                    {
                        hasExcludeItemMatched = true;
                        if (isExcludeOperationOR == config.first.contains(key.trimmed()))
                        {
                            includeConfig = !isExcludeOperationOR;
                            break;
                        }
                    }
                }
                if (!hasExcludeItemMatched)
                    includeConfig = true;
            }

            if (includeConfig)
                filteredConnections << config;
        }

        const auto useFilteredConnections =
            filteredConnections.count() > 5 ||
            QvMessageBoxAsk(nullptr, tr("Update Subscription"),
                            tr("%1 out of %n entrie(s) have been filtered out, do you want to continue?", "", newConnections.count())
                                    .arg(filteredConnections.count()) +
                                NEWLINE + GetDisplayName(id)) == Yes;
        const auto &selectedConnections = useFilteredConnections ? filteredConnections : newConnections;

        QMultiMap<QString, ConnectionId> nameMap;
        QMultiMap<std::tuple<QString, QString, int>, ConnectionId> typeMap;
        const auto originalGroupConnections = groups[id].connections;
        auto unmatchedOriginalConnections = originalGroupConnections;
        for (const auto &conn : originalGroupConnections)
        {
            nameMap.insert(GetDisplayName(conn), conn);
            const auto &&[protocol, host, port] = GetConnectionInfo(conn);
            if (port != 0)
                typeMap.insert({ protocol, host, port }, conn);
        }

        struct PlannedConnection
        {
            QString alias;
            CONFIGROOT root;
            ConnectionId id;
            bool isNew = false;
            bool rename = false;
            QString oldName;
            CONFIGROOT oldRoot;
        };

        QList<PlannedConnection> plans;
        QList<ConnectionId> replacementIds;
        const auto removeValueFromNameMap = [&](const ConnectionId &connectionId) {
            for (auto it = nameMap.begin(); it != nameMap.end();)
                it = it.value() == connectionId ? nameMap.erase(it) : ++it;
        };
        const auto removeValueFromTypeMap = [&](const ConnectionId &connectionId) {
            for (auto it = typeMap.begin(); it != typeMap.end();)
                it = it.value() == connectionId ? typeMap.erase(it) : ++it;
        };

        for (const auto &config : selectedConnections)
        {
            const auto &alias = config.first;
            bool canGetOutboundData = false;
            const auto &&[protocol, host, port] = GetConnectionInfo(config.second, &canGetOutboundData);
            const auto outboundData = std::make_tuple(protocol, host, port);

            PlannedConnection plan;
            plan.alias = alias;
            plan.root = config.second;

            if (nameMap.contains(alias))
            {
                plan.id = nameMap.take(alias);
                removeValueFromTypeMap(plan.id);
                unmatchedOriginalConnections.removeAll(plan.id);
            }
            else if (canGetOutboundData && typeMap.contains(outboundData))
            {
                plan.id = typeMap.take(outboundData);
                removeValueFromNameMap(plan.id);
                unmatchedOriginalConnections.removeAll(plan.id);
                plan.rename = connections[plan.id].displayName != alias;
            }
            else
            {
                do
                {
                    plan.id = ConnectionId{ GenerateUuid() };
                } while (connections.contains(plan.id) || replacementIds.contains(plan.id));
                plan.isNew = true;
            }

            if (!plan.isNew)
            {
                if (!connectionRootCache.contains(plan.id))
                {
                    LOG("Cannot update subscription because an existing connection root is unavailable: " + plan.id.toString());
                    return false;
                }
                plan.oldName = connections[plan.id].displayName;
                plan.oldRoot = connectionRootCache.value(plan.id);
            }

            replacementIds.append(plan.id);
            plans.append(plan);
        }

        bool removeUnmatched = false;
        if (!unmatchedOriginalConnections.isEmpty())
        {
            removeUnmatched = QvMessageBoxAsk(nullptr, tr("Update Subscription"),
                                              tr("There're %n connection(s) in the group that do not belong the current subscription (any more).",
                                                 "", unmatchedOriginalConnections.count()) +
                                                  NEWLINE + GetDisplayName(id) + NEWLINE + tr("Would you like to remove them?")) == Yes;
        }

        const auto membership = data_safety::BuildSubscriptionMembership(originalGroupConnections, replacementIds, removeUnmatched);
        QList<int> writtenPlans;
        const auto connectionPath = [](const ConnectionId &connectionId) {
            return QV2RAY_CONNECTIONS_DIR + connectionId.toString() + QV2RAY_CONFIG_FILE_EXTENSION;
        };
        const auto rollbackRootWrites = [&]() {
            for (auto i = writtenPlans.crbegin(); i != writtenPlans.crend(); ++i)
            {
                const auto &plan = plans[*i];
                const auto path = connectionPath(plan.id);
                if (plan.isNew)
                {
                    if (QFile::exists(path) && !QFile::remove(path))
                        LOG("Failed to remove staged subscription connection during rollback: " + path);
                }
                else if (!StringToFile(JsonToString(plan.oldRoot), path))
                {
                    LOG("CRITICAL: failed to restore connection config during subscription rollback: " + plan.id.toString());
                }
            }
        };

        for (int i = 0; i < plans.count(); ++i)
        {
            const auto &plan = plans[i];
            if (!StringToFile(JsonToString(plan.root), connectionPath(plan.id)))
            {
                LOG("Subscription update aborted because a connection config could not be written: " + plan.id.toString());
                rollbackRootWrites();
                return false;
            }
            writtenPlans.append(i);
        }

        const auto oldGroup = groups[id];
        const auto oldConnections = connections;
        const auto oldRootCache = connectionRootCache;
        const auto now = system_clock::to_time_t(system_clock::now());

        for (const auto &plan : plans)
        {
            if (plan.isNew)
            {
                ConnectionObject object;
                object.creationDate = now;
                object.lastUpdatedDate = now;
                object.lastConnected = 0;
                object.displayName = plan.alias;
                object.__qvConnectionRefCount = 0;
                connections.insert(plan.id, object);
            }
            else if (plan.rename)
            {
                connections[plan.id].displayName = plan.alias;
            }
            connectionRootCache[plan.id] = plan.root;
        }

        groups[id].connections = membership.finalConnections;
        groups[id].lastUpdatedDate = now;

        for (const auto &connectionId : membership.added)
            connections[connectionId].__qvConnectionRefCount++;
        for (const auto &connectionId : membership.removed)
            connections[connectionId].__qvConnectionRefCount--;

        QList<ConnectionId> fullyRemoved;
        for (const auto &connectionId : membership.removed)
        {
            if (connections.contains(connectionId) && connections[connectionId].__qvConnectionRefCount <= 0)
            {
                fullyRemoved.append(connectionId);
                connections.remove(connectionId);
                connectionRootCache.remove(connectionId);
            }
        }

        if (!SaveConnectionConfig())
        {
            groups[id] = oldGroup;
            connections = oldConnections;
            connectionRootCache = oldRootCache;
            rollbackRootWrites();
            QvMessageBoxWarn(nullptr, tr("Subscription update failed"),
                             tr("The updated subscription could not be committed safely. The previous connection state was restored."));
            return false;
        }

        for (const auto &connectionId : fullyRemoved)
        {
            const auto path = connectionPath(connectionId);
            if (QFile::exists(path) && !QFile::remove(path))
                LOG("Failed to remove an unreferenced connection file after committing subscription update: " + path);
        }

        for (const auto &plan : plans)
        {
            if (plan.isNew)
            {
                emit OnConnectionCreated({ plan.id, id }, plan.alias);
                PluginHost->SendEvent({ Events::ConnectionEntry::Created, plan.alias, "" });
            }
            else
            {
                if (plan.rename)
                {
                    emit OnConnectionRenamed(plan.id, plan.oldName, plan.alias);
                    PluginHost->SendEvent({ Events::ConnectionEntry::Renamed, plan.alias, plan.oldName });
                }
                emit OnConnectionModified(plan.id);
                PluginHost->SendEvent({ Events::ConnectionEntry::Edited, connections[plan.id].displayName, "" });
            }
        }

        for (const auto &connectionId : membership.removed)
        {
            const auto oldName = oldConnections.value(connectionId).displayName;
            PluginHost->SendEvent({ Events::ConnectionEntry::RemovedFromGroup, oldName, "" });
            emit OnConnectionRemovedFromGroup({ connectionId, id });
        }

        return true;
    }

    void QvConfigHandler::p_OnStatsDataArrived(const ConnectionGroupPair &id, const QMap<StatisticsType, QvStatsSpeed> &data)
    {
        if (id.isEmpty())
            return;

        const auto &cid = id.connectionId;
        QMap<StatisticsType, QvStatsSpeedData> result;
        for (const auto t : data.keys())
        {
            const auto &stat = data[t];
            connections[cid].stats[t].upLinkData += stat.first;
            connections[cid].stats[t].downLinkData += stat.second;
            result[t] = { stat, connections[cid].stats[t].toData() };
        }

        emit OnStatsAvailable(id, result);
        PluginHost->SendEvent({ GetDisplayName(cid),                     //
                                result[CurrentStatAPIType].first.first,  //
                                result[CurrentStatAPIType].first.second, //
                                result[CurrentStatAPIType].second.first, //
                                result[CurrentStatAPIType].second.second });
    }

    const ConnectionGroupPair QvConfigHandler::CreateConnection(const CONFIGROOT &root, const QString &displayName, const GroupId &groupId,
                                                                bool skipSaveConfig)
    {
        CheckValidId(groupId, {});
        LOG("Creating new connection: " + displayName);
        ConnectionId newId;
        do
        {
            newId = ConnectionId{ GenerateUuid() };
        } while (connections.contains(newId));

        const auto path = QV2RAY_CONNECTIONS_DIR + newId.toString() + QV2RAY_CONFIG_FILE_EXTENSION;
        if (!StringToFile(JsonToString(root), path))
        {
            LOG("Failed to persist new connection config: " + displayName);
            return {};
        }

        groups[groupId].connections << newId;
        connections[newId].creationDate = system_clock::to_time_t(system_clock::now());
        connections[newId].lastConnected = 0;
        connections[newId].displayName = displayName;
        connections[newId].__qvConnectionRefCount = 1;
        connectionRootCache[newId] = root;

        if (!skipSaveConfig && !SaveConnectionConfig())
        {
            groups[groupId].connections.removeAll(newId);
            connections.remove(newId);
            connectionRootCache.remove(newId);
            if (QFile::exists(path) && !QFile::remove(path))
                LOG("Failed to remove new connection file after metadata save failure: " + path);
            return {};
        }

        emit OnConnectionCreated({ newId, groupId }, displayName);
        PluginHost->SendEvent({ Events::ConnectionEntry::Created, displayName, "" });
        return { newId, groupId };
    }

} // namespace Qv2ray::core::handler

#undef CheckIdExistance
#undef CheckGroupExistanceEx
#undef CheckConnectionExistanceEx
