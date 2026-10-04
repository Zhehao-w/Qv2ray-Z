#pragma once
#include "core/connection/OutboundEditorPersistence.hpp"
#include "ui/common/LogHighlighter.hpp"
#include "ui/common/QvMessageBus.hpp"
#include "ui/common/speedchart/speedwidget.hpp"
#include "ui/widgets/common/WidgetUIBase.hpp"
#include "ui/widgets/models/ConnectionModelHelper.hpp"
#include "ui/widgets/widgets/ConnectionInfoWidget.hpp"
#include "ui/widgets/widgets/ConnectionItemWidget.hpp"
#include "ui_w_MainWindow.h"

#include <QHostAddress>
#include <QMainWindow>
#include <QMenu>

namespace Qv2rayPlugin
{
    class QvPluginMainWindowWidget;
}

class MainWindow
    : public QMainWindow
    , QvStateObject
    , Ui::MainWindow
{
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    void ProcessCommand(QString command, QStringList commands, QMap<QString, QString> args);

  signals:
    void StartConnection() const;
    void StopConnection() const;
    void RestartConnection() const;

  private:
    QvMessageBusSlotDecl;
  private slots:
    void on_activatedTray(QSystemTrayIcon::ActivationReason reason);
    void on_preferencesBtn_clicked();
    void on_setBypassCNBtn_clicked();
    void on_clearBypassCNBtn_clicked();
    void on_clearlogButton_clicked();
    void on_connectionTreeView_customContextMenuRequested(const QPoint &pos);
    void on_importConfigButton_clicked();
    void on_subsButton_clicked();
    void on_connectionFilterTxt_textEdited(const QString &arg1);
    void on_connectionTreeView_clicked(const QModelIndex &index);
    void on_connectionTreeView_doubleClicked(const QModelIndex &index);
    void on_chartVisibilityBtn_clicked();
    void on_logVisibilityBtn_clicked();
    void on_clearChartBtn_clicked();
    void on_masterLogBrowser_textChanged();
    void on_collapseGroupsBtn_clicked();
    //
    void OnEditRequested(const ConnectionId &id);
    void OnEditJsonRequested(const ConnectionId &id);
    void OnLogScrollbarValueChanged(int value);
    void OnPluginButtonClicked();
    void OnRecentConnectionsMenuReadyToShow();
    //
    void Action_CopyGraphAsImage();
    void Action_CopyRecentLogs();
    void Action_DeleteConnections();
    void Action_DuplicateConnection();
    void Action_Edit();
    void Action_EditComplex();
    void Action_EditJson();
    void Action_Exit();
    void Action_RenameConnection();
    void Action_ResetStats();
    void Action_SetAutoConnection();
    void Action_Start();
    void Action_TestLatency();
    void Action_TestRealLatency();
    void Action_UpdateSubscription();

  private:
    void SortConnectionList(ConnectionInfoRole byCol, bool asending);
    void ReloadRecentConnectionList();
    //
    void OnConnected(const ConnectionGroupPair &id);
    void OnDisconnected(const ConnectionGroupPair &id);
    void OnStatsAvailable(const ConnectionGroupPair &id, const QMap<StatisticsType, QvStatsSpeedData> &data);
    void OnVCoreLogAvailable(const ConnectionGroupPair &id, const QString &log);
    //
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *event) override;
    void timerEvent(QTimerEvent *event) override;
    void changeEvent(QEvent *e) override;
    //
    ConnectionListHelper *modelHelper;
    ConnectionInfoWidget *infoWidget;
    SpeedWidget *speedChartWidget;
    SyntaxHighlighter *vCoreLogHighlighter;
    //
    ConnectionGroupPair lastConnected;
    int qvLogTimerId;
    bool qvLogAutoScoll = true;
    QList<Qv2rayPlugin::QvPluginMainWindowWidget *> pluginWidgets;
};
