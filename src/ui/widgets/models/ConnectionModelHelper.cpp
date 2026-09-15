#include "ConnectionModelHelper.hpp"

#include "core/handler/ConfigHandler.hpp"
#include "ui/widgets/widgets/ConnectionItemWidget.hpp"

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
        for (const auto &gid : ConnectionManager->GetConnectionContainedIn(id))
        {
            ConnectionGroupPair pair{ id, gid };
            if (pairs.contains(pair))
                pairs[pair]->setData(newName, ROLE_DISPLAYNAME);
        }
    };

    const auto latencyLambda = [&](const ConnectionId &id, const int avg) {
        for (const auto &gid : ConnectionManager->GetConnectionContainedIn(id))
        {
            ConnectionGroupPair pair{ id, gid };
            if (pairs.contains(pair))
                pairs[pair]->setData(NumericString(avg), ROLE_LATENCY);
        }
    };

    const auto statsLambda = [&](const ConnectionGroupPair &id, const QMap<StatisticsType, QvStatsSpeedData> &data) {
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
}

void ConnectionListHelper::SetGrouped(bool grouped)
{
    if (groupedView == grouped && !pairs.isEmpty())
        return;
    groupedView = grouped;
    rebuild();
}

void ConnectionListHelper::rebuild()
{
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
            if (!groupedView && added.contains(connection))
            {
                pairs[{ connection, group }] = connections[connection].first();
                continue;
            }
            addConnectionItem({ connection, group });
            added.insert(connection);
        }
    }
    Filter(filterText);
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
        for (auto it = pairs.cbegin(); it != pairs.cend(); ++it)
        {
            const auto index = model->indexFromItem(it.value());
            const auto widget = static_cast<ConnectionItemWidget *>(parentView->indexWidget(index));
            parentView->setRowHidden(index.row(), index.parent(), !widget->NameMatched(key));
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
    const auto index = model->indexFromItem(item);
    if (!index.isValid())
        return;
    model->removeRow(index.row(), index.parent());
    connections[id.connectionId].removeAll(item);
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
    const auto index = model->indexFromItem(item);
    if (!index.isValid())
        return;
    model->removeRow(index.row(), index.parent());
}
