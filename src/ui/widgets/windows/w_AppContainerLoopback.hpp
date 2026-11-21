#pragma once

#include "platform/windows/AppContainerLoopback.hpp"

#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabBar;
class QTreeWidget;
class QTreeWidgetItem;

class AppContainerLoopbackDialog final : public QDialog
{
    Q_OBJECT

  public:
    explicit AppContainerLoopbackDialog(QWidget *parent);

  private:
    struct OperationResult
    {
        std::optional<Qv2ray::platform::windows::loopback::Inventory> inventory;
        Qv2ray::platform::windows::loopback::Exemptions exemptions;
        unsigned long changeError = 0;
        int unverified = 0;
    };

    void reject() override;
    void refresh();
    void run(const QString &message, std::function<OperationResult()> operation);
    void rebuildList();
    void updateActions();
    QSet<QString> selectedSids() const;
    void change(const QSet<QString> &targets, bool enable, bool all = false);

    QLineEdit *search;
    QComboBox *filter;
    QTabBar *tabs;
    QTreeWidget *tree;
    QLabel *status;
    QLabel *summary;
    QPushButton *refreshButton;
    QPushButton *enableAll;
    QPushButton *disableAll;
    QPushButton *enableSelected;
    QPushButton *disableSelected;
    QPushButton *closeButton;
    Qv2ray::platform::windows::loopback::Inventory inventory;
    QSet<QString> exemptSids;
    QSet<QString> detectedSids;
    bool busy = false;
    bool stateKnown = false;
};
