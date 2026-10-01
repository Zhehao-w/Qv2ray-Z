#include "ConnectionModelHelper.hpp"

#include "core/handler/ConfigHandler.hpp"
#include "ui/widgets/widgets/ConnectionItemWidget.hpp"

#include <QTimer>

#define NumericString(i) (QString("%1").arg(i, 30, 10, QLatin1Char('0')))

ConnectionListHelper::ConnectionListHelper(QTreeView *view, QObject *parent) : QObject(parent)
{
    parentView = view;
    model = new QStandardItemModel();
    view->setModel(model);
    for (const auto &group : ConnectionManager->AllGroups())
    {
        addGroupItem(group);
        for (const auto &connection : ConnectionManager->GetConnections(group))
        {
            addConnectionItem({ connection, group });
        }
    }
    const auto renamedLambda = [&](const ConnectionId &id, const QString &, const QString &newName) {
        for (const auto &gid : ConnectionManager->AllGroups())
        {
            if (!ConnectionManager->GetConnections(gid).contains(id))
                continue;
            ConnectionGroupPair pair{ id, gid };
            if (pairs.contains(pair))
                pairs[pair]->setData(newName, ROLE_DISPLAYNAME);
        }
    };

    const auto latencyLambda = [&](const ConnectionId &id, const int avg) {
        for (const auto &gid : ConnectionManager->AllGroups())
        {
            if (!ConnectionManager->GetConnections(gid).contains(id))
                continue;
            ConnectionGroupPair pair{ id, gid };
            if (pairs.contains(pair))
                pairs[pair]->setData(NumericString(avg), ROLE_LATENCY);
        }
    };

    const auto statsLambda = [&](const ConnectionGroupPair &id, const QMap<StatisticsType, QvStatsSpeedData> &data) {
        Q_UNUSED(data)
        if (connections.contains(id.connectionId))
        {
            for (const auto &index : connections[id.connectionId])
                index->setData(NumericString(GetConnectionTotalData(id.connectionId)), ROLE_DATA_USAGE);
        }
    };

    connect(ConnectionManager, &QvConfigHandler::OnConnectionRemovedFromGroup, this, &ConnectionListHelper::OnConnectionDeleted);
    connect(ConnectionManager, &QvConfigHandler::OnConnectionCreated, this, &ConnectionListHelper::OnConnectionCreated);
    connect(ConnectionManager, &QvConfigHandler::OnConnectionLinkedWithGroup, this, &ConnectionListHelper::OnConnectionLinkedWithGroup);
    connect(ConnectionManager, &QvConfigHandler::OnGroupCreated, this, &ConnectionListHelper::OnGroupCreated);
    connect(ConnectionManager, &QvConfigHandler::OnGroupDeleted, this, &ConnectionListHelper::OnGroupDeleted);
    connect(ConnectionManager, &QvConfigHandler::OnConnectionRenamed, renamedLambda);
    connect(ConnectionManager, &QvConfigHandler::OnLatencyTestFinished, latencyLambda);
    connect(ConnectionManager, &QvConfigHandler::OnStatsAvailable, statsLambda);
    sanitizeStoredContexts();
}

QModelIndex ConnectionListHelper::GetConnectionPairIndex(const ConnectionGroupPair &id)
{
    auto item = pairs.value(id, nullptr);
    if (!item)
        return {};
    const auto index = model->indexFromItem(item);
    if (!groupedView && ConnectionManager->IsValidId(id))
    {
        if (auto widget = qobject_cast<ConnectionItemWidget *>(parentView->indexWidget(index)))
            widget->SetIdentifier(id);
    }
    return index;
}

ConnectionGroupPair ConnectionListHelper::currentSelection() const
{
    const auto index = parentView->currentIndex();
    if (!index.isValid())
        return {};
    const auto widget = qobject_cast<ConnectionItemWidget *>(parentView->indexWidget(index));
    if (!widget || !widget->IsConnection())
        return {};
    const auto id = widget->Identifier();
    if (ConnectionManager->IsValidId(id))
        return id;
    return ConnectionManager->ResolveConnectionContext(id.connectionId, id);
}

void ConnectionListHelper::SetGrouped(bool grouped)
{
    const auto preferred = currentSelection();
    if (groupedView == grouped && !pairs.isEmpty())
    {
        sanitizeStoredContexts();
        return;
    }
    groupedView = grouped;
    rebuild(preferred);
}

void ConnectionListHelper::sanitizeStoredContexts()
{
    if (!GlobalConfig.lastConnectedId.isEmpty() && !ConnectionManager->IsValidId(GlobalConfig.lastConnectedId))
    {
        if (ConnectionManager->IsValidId(GlobalConfig.lastConnectedId.connectionId))
            GlobalConfig.lastConnectedId = ConnectionManager->ResolveConnectionContext(GlobalConfig.lastConnectedId.connectionId, GlobalConfig.lastConnectedId);
        else
            GlobalConfig.lastConnectedId.clear();
    }

    if (GlobalConfig.autoStartBehavior == AUTO_CONNECTION_FIXED && !GlobalConfig.autoStartId.isEmpty() &&
        !ConnectionManager->IsValidId(GlobalConfig.autoStartId))
    {
        if (ConnectionManager->IsValidId(GlobalConfig.autoStartId.connectionId))
            GlobalConfig.autoStartId = ConnectionManager->ResolveConnectionContext(GlobalConfig.autoStartId.connectionId, GlobalConfig.autoStartId);
        else
            GlobalConfig.autoStartId.clear();
    }

    QList<ConnectionGroupPair> validRecent;
    for (const auto &item : GlobalConfig.uiConfig.recentConnections)
    {
        if (ConnectionManager->IsValidId(item) && !validRecent.contains(item))
            validRecent.append(item);
    }
    GlobalConfig.uiConfig.recentConnections = validRecent;
    scheduleCurrentContextRepair();
}

void ConnectionListHelper::scheduleCurrentContextRepair()
{
    const auto current = ConnectionManager->CurrentConnection();
    if (current.isEmpty() || ConnectionManager->IsValidId(current) || contextRepairScheduled)
        return;

    contextRepairScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        contextRepairScheduled = false;
        const auto staleCurrent = ConnectionManager->CurrentConnection();
        if (staleCurrent.isEmpty() || ConnectionManager->IsValidId(staleCurrent))
            return;

        const auto fallback = ConnectionManager->ResolveConnectionContext(staleCurrent.connectionId, GlobalConfig.lastConnectedId);
        ConnectionManager->StopConnection();
        if (!fallback.isEmpty())
            ConnectionManager->StartConnection(fallback);
    });
}

void ConnectionListHelper::rebuild(const ConnectionGroupPair &preferredContext)
{
    auto preferred = preferredContext;
    if (preferred.isEmpty())
        preferred = currentSelection();

    sanitizeStoredContexts();
    model->clear();
    groups.clear();
    pairs.clear();
    connections.clear();
    QSet<ConnectionId> added;
    for (const auto &group : ConnectionManager->AllGroups())
    {
        if (groupedView)
            addGroupItem(group);
        for (const auto &connection : ConnectionManager->GetConnections(group))
        {
            const ConnectionGroupPair encountered{ connection, group };
            if (!groupedView)
            {
                if (added.contains(connection))
                {
                    pairs[encountered] = connections[connection].first();
                    continue;
                }

                const auto explicitPreference = preferred.connectionId == connection ? preferred : ConnectionGroupPair{};
                const auto resolved = ConnectionManager->ResolveConnectionContext(connection, explicitPreference);
                if (resolved.isEmpty())
                    continue;
                auto item = addConnectionItem(resolved);
                pairs[encountered] = item;
                added.insert(connection);
                continue;
            }
            addConnectionItem(encountered);
            added.insert(connection);
        }
    }
    Filter(filterText);

    if (!preferred.isEmpty() && ConnectionManager->IsValidId(preferred))
    {
        const auto index = GetConnectionPairIndex(preferred);
        if (index.isValid())
        {
            parentView->setCurrentIndex(index);
            parentView->scrollTo(index);
            parentView->clicked(index);
        }
    }
}

ConnectionListHelper::~ConnectionListHelper()
{
    delete model;
}

void ConnectionListHelper::Sort(ConnectionInfoRole role, Qt::SortOrder order)
{
    model->setSortRole(role);
    model->sort(0, order);
}

void ConnectionListHelper::Filter(const QString &key)
{
    filterText = key;
    if (!groupedView)
    {
        const auto normalized = key.toLower();
        for (auto it = connections.cbegin(); it != connections.cend(); ++it)
        {
            if (it.value().isEmpty())
                continue;
            bool matches = GetDisplayName(it.key()).toLower().contains(normalized);
            if (!matches)
            {
                for (const auto &group : ConnectionManager->AllGroups())
                {
                    if (ConnectionManager->GetConnections(group).contains(it.key()) && GetDisplayName(group).toLower().contains(normalized))
                    {
                        matches = true;
                        break;
                    }
                }
            }
            const auto index = model->indexFromItem(it.value().first());
            parentView->setRowHidden(index.row(), index.parent(), !matches);
        }
        return;
    }
    for (const auto &groupId : ConnectionManager->AllGroups())
    {
        if (!groups.contains(groupId))
            continue;
        const auto groupItem = model->indexFromItem(groups[groupId]);
        bool anyVisible = false;
        for (const auto &connectionId : ConnectionManager->GetConnections(groupId))
        {
            if (!pairs.contains({ connectionId, groupId }))
                continue;
            const auto connectionItem = model->indexFromItem(pairs[{ connectionId, groupId }]);
            const auto matches = static_cast<ConnectionItemWidget *>(parentView->indexWidget(connectionItem))->NameMatched(key);
            parentView->setRowHidden(connectionItem.row(), connectionItem.parent(), !matches);
            anyVisible |= matches;
        }
        parentView->setRowHidden(groupItem.row(), groupItem.parent(), !anyVisible);
        if (anyVisible && !key.isEmpty())
            parentView->expand(groupItem);
    }
}

QStandardItem *ConnectionListHelper::addConnectionItem(const ConnectionGroupPair &id)
{
    // Create Standard Item
    auto connectionItem = new QStandardItem();
    connectionItem->setData(GetDisplayName(id.connectionId), ConnectionInfoRole::ROLE_DISPLAYNAME);
    connectionItem->setData(NumericString(GetConnectionLatency(id.connectionId)), ConnectionInfoRole::ROLE_LATENCY);
    connectionItem->setData(NumericString(GetConnectionTotalData(id.connectionId)), ConnectionInfoRole::ROLE_DATA_USAGE);
    //
    // Find groups
    if (groupedView)
    {
        const auto groupIndex = groups.contains(id.groupId) ? groups[id.groupId] : addGroupItem(id.groupId);
        groupIndex->appendRow(connectionItem);
    }
    else
    {
        model->appendRow(connectionItem);
    }
    const auto connectionIndex = connectionItem->index();
    //
    auto widget = new ConnectionItemWidget(id, parentView);
    widget->SetFlexibleContext(!groupedView);
    connect(widget, &ConnectionItemWidget::RequestWidgetFocus, [widget, connectionIndex, this]() {
        parentView->setCurrentIndex(connectionIndex);
        parentView->scrollTo(connectionIndex);
        parentView->clicked(connectionIndex);
    });
    //
    parentView->setIndexWidget(connectionIndex, widget);
    pairs[id] = connectionItem;
    connections[id.connectionId].append(connectionItem);
    return connectionItem;
}

QStandardItem *ConnectionListHelper::addGroupItem(const GroupId &groupId)
{
    // Create Item
    const auto item = new QStandardItem();
    // Set item into model
    model->appendRow(item);
    // Get item index
    const auto index = item->index();
    parentView->setIndexWidget(index, new ConnectionItemWidget(groupId, parentView));
    groups[groupId] = item;
    return item;
}

void ConnectionListHelper::OnConnectionCreated(const ConnectionGroupPair &id, const QString &)
{
    Q_UNUSED(id)
    rebuild();
}

void ConnectionListHelper::OnConnectionDeleted(const ConnectionGroupPair &id)
{
    if (!groupedView)
    {
        Q_UNUSED(id)
        rebuild();
        return;
    }
    auto item = pairs.take(id);
    if (!item)
    {
        sanitizeStoredContexts();
        return;
    }
    const auto index = model->indexFromItem(item);
    if (index.isValid())
        model->removeRow(index.row(), index.parent());
    connections[id.connectionId].removeAll(item);
    if (connections[id.connectionId].isEmpty())
        connections.remove(id.connectionId);
    sanitizeStoredContexts();
}

void ConnectionListHelper::OnConnectionLinkedWithGroup(const ConnectionGroupPair &pairId)
{
    Q_UNUSED(pairId)
    rebuild();
}

void ConnectionListHelper::OnGroupCreated(const GroupId &id, const QString &)
{
    Q_UNUSED(id)
    rebuild();
}

void ConnectionListHelper::OnGroupDeleted(const GroupId &id, const QList<ConnectionId> &connections)
{
    if (!groupedView)
    {
        Q_UNUSED(id)
        Q_UNUSED(connections)
        rebuild();
        return;
    }
    for (const auto &conn : connections)
    {
        const ConnectionGroupPair pair{ conn, id };
        OnConnectionDeleted(pair);
    }
    const auto item = groups.take(id);
    if (item)
    {
        const auto index = model->indexFromItem(item);
        if (index.isValid())
            model->removeRow(index.row(), index.parent());
    }
    sanitizeStoredContexts();
}
