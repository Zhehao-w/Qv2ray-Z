#include "ConnectionInfoWidget.hpp"

#include "core/CoreUtils.hpp"
#include "ui/widgets/common/WidgetUIBase.hpp"
#include "utils/QvHelpers.hpp"

constexpr auto INDEX_CONNECTION = 0;
constexpr auto INDEX_GROUP = 1;

QvMessageBusSlotImpl(ConnectionInfoWidget)
{
    switch (msg)
    {
        MBRetranslateDefaultImpl;
        MBUpdateColorSchemeDefaultImpl;
        case HIDE_WINDOWS:
        case SHOW_WINDOWS: break;
    }
}

void ConnectionInfoWidget::updateConnectionAction()
{
    const auto isCurrentItem = KernelInstance->CurrentConnection().connectionId == connectionId;
    connectBtn->setIcon(QIcon(isCurrentItem ? QV2RAY_COLORSCHEME_FILE("stop") : QV2RAY_COLORSCHEME_FILE("start")));
    connectBtn->setText(isCurrentItem ? tr("Disconnect") : tr("Connect"));
}

void ConnectionInfoWidget::updateColorScheme()
{
    latencyBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("ping_gauge")));
    deleteBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("ashbin")));
    editBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("edit")));
    editJsonBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("code")));
    updateConnectionAction();
}

ConnectionInfoWidget::ConnectionInfoWidget(QWidget *parent) : QWidget(parent)
{
    setupUi(this);
    QvMessageBusConnect(ConnectionInfoWidget);
    updateColorScheme();

    connect(ConnectionManager, &QvConfigHandler::OnConnected, this, &ConnectionInfoWidget::OnConnected);
    connect(ConnectionManager, &QvConfigHandler::OnDisconnected, this, &ConnectionInfoWidget::OnDisConnected);
    connect(ConnectionManager, &QvConfigHandler::OnGroupRenamed, this, &ConnectionInfoWidget::OnGroupRenamed);
    connect(ConnectionManager, &QvConfigHandler::OnConnectionModified, this, &ConnectionInfoWidget::OnConnectionModified);
    connect(ConnectionManager, &QvConfigHandler::OnConnectionLinkedWithGroup, this, &ConnectionInfoWidget::OnConnectionModified_Pair);
    connect(ConnectionManager, &QvConfigHandler::OnConnectionRemovedFromGroup, this, &ConnectionInfoWidget::OnConnectionModified_Pair);
}

void ConnectionInfoWidget::ShowDetails(const ConnectionGroupPair &_identifier)
{
    groupId = _identifier.groupId;
    connectionId = _identifier.connectionId;
    const auto isConnection = connectionId != NullConnectionId;

    editBtn->setEnabled(isConnection);
    editJsonBtn->setEnabled(isConnection);
    connectBtn->setEnabled(isConnection);
    editBtn->setVisible(isConnection);
    editJsonBtn->setVisible(isConnection);
    connectBtn->setVisible(isConnection);
    stackedWidget->setCurrentIndex(isConnection ? INDEX_CONNECTION : INDEX_GROUP);

    if (isConnection)
    {
        connNameLabel->setText(GetDisplayName(connectionId));
        protocolLabel->setText(GetConnectionProtocolString(connectionId));
        groupLabel->setText(GetDisplayName(groupId, 175));

        auto [protocol, host, port] = GetConnectionInfo(connectionId);
        Q_UNUSED(protocol)
        addressLabel->setText(host);
        portLabel->setNum(port);
        updateConnectionAction();
    }
    else
    {
        connNameLabel->setText(GetDisplayName(groupId));
        groupNameLabel->setText(GetDisplayName(groupId));
        const auto &groupMetaData = ConnectionManager->GetGroupMetaObject(groupId);
        groupSubsLinkTxt->setText(groupMetaData.isSubscription ? groupMetaData.subscriptionOption.address : tr("Not a subscription"));
    }
}

ConnectionInfoWidget::~ConnectionInfoWidget()
{
}

void ConnectionInfoWidget::OnConnectionModified(const ConnectionId &id)
{
    if (id == connectionId)
        ShowDetails({ id, groupId });
}

void ConnectionInfoWidget::OnConnectionModified_Pair(const ConnectionGroupPair &id)
{
    if (id.connectionId == connectionId && id.groupId == groupId)
        ShowDetails(id);
}

void ConnectionInfoWidget::OnGroupRenamed(const GroupId &id, const QString &oldName, const QString &newName)
{
    Q_UNUSED(oldName)
    if (groupId == id)
    {
        groupNameLabel->setText(newName);
        groupLabel->setText(newName);
        if (connectionId == NullConnectionId)
            connNameLabel->setText(newName);
    }
}

void ConnectionInfoWidget::on_connectBtn_clicked()
{
    if (ConnectionManager->IsConnected({ connectionId, groupId }))
        ConnectionManager->StopConnection();
    else
        ConnectionManager->StartConnection({ connectionId, groupId });
}

void ConnectionInfoWidget::on_editBtn_clicked()
{
    emit OnEditRequested(connectionId);
}

void ConnectionInfoWidget::on_editJsonBtn_clicked()
{
    emit OnJsonEditRequested(connectionId);
}

void ConnectionInfoWidget::on_deleteBtn_clicked()
{
    if (QvMessageBoxAsk(this, tr("Delete an item"), tr("Are you sure to delete the current item?")) == Yes)
    {
        if (connectionId != NullConnectionId)
            ConnectionManager->RemoveConnectionFromGroup(connectionId, groupId);
        else
            ConnectionManager->DeleteGroup(groupId);
    }
}

void ConnectionInfoWidget::OnConnected(const ConnectionGroupPair &id)
{
    if (id == ConnectionGroupPair{ connectionId, groupId })
        updateConnectionAction();
}

void ConnectionInfoWidget::OnDisConnected(const ConnectionGroupPair &id)
{
    if (id == ConnectionGroupPair{ connectionId, groupId })
        updateConnectionAction();
}

void ConnectionInfoWidget::on_latencyBtn_clicked()
{
    if (connectionId != NullConnectionId)
        ConnectionManager->StartLatencyTest(connectionId);
    else
        ConnectionManager->StartLatencyTest(groupId);
}
