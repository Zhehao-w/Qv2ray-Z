#pragma once
#include "base/Qv2rayBase.hpp"

#include <QStandardItem>
#include <QStandardItemModel>
#include <QTreeView>

namespace Qv2ray::ui::widgets::models
{
    enum ConnectionInfoRole
    {
        // 10 -> Magic value.
        ROLE_DISPLAYNAME = Qt::UserRole + 10,
        ROLE_LATENCY,
        ROLE_IMPORTTIME,
        ROLE_LAST_CONNECTED_TIME,
        ROLE_DATA_USAGE
    };

    class ConnectionListHelper : public QObject
    {
        Q_OBJECT
      public:
        ConnectionListHelper(QTreeView *parentView, QObject *parent = nullptr);
        ~ConnectionListHelper();
        void Sort(ConnectionInfoRole, Qt::SortOrder);
        void Filter(const QString &);
        void SetGrouped(bool grouped);
        bool IsGrouped() const { return groupedView; }

        QModelIndex GetConnectionPairIndex(const ConnectionGroupPair &id);

        inline QModelIndex GetGroupIndex(const GroupId &id) const
        {
            return model->indexFromItem(groups.value(id, nullptr));
        }

      private:
        QStandardItem *addConnectionItem(const ConnectionGroupPair &id);
        QStandardItem *addGroupItem(const GroupId &groupId);
        void OnGroupCreated(const GroupId &id, const QString &displayName);
        void OnGroupDeleted(const GroupId &id, const QList<ConnectionId> &connections);
        void OnConnectionCreated(const ConnectionGroupPair &Id, const QString &displayName);
        void OnConnectionDeleted(const ConnectionGroupPair &Id);
        void OnConnectionLinkedWithGroup(const ConnectionGroupPair &id);
        ConnectionGroupPair currentSelection() const;
        void sanitizeStoredContexts();

      private:
        QTreeView *parentView;
        QStandardItemModel *model;
        //
        QHash<GroupId, QStandardItem *> groups;
        QHash<ConnectionGroupPair, QStandardItem *> pairs;
        QHash<ConnectionId, QList<QStandardItem *>> connections;
        bool groupedView = true;
        QString filterText;
        void rebuild(const ConnectionGroupPair &preferredContext = {});
    };

} // namespace Qv2ray::ui::widgets::models

using namespace Qv2ray::ui::widgets::models;
