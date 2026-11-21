#include "w_AppContainerLoopback.hpp"

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabBar>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace Loopback = Qv2ray::platform::windows::loopback;

namespace
{
    constexpr int ExemptionColumn = 2;
    constexpr int SidsRole = Qt::UserRole;
    constexpr int ItemKeyRole = Qt::UserRole + 1;
    constexpr unsigned long UserCancelled = 1223; // ERROR_CANCELLED

    QSet<QString> ItemSids(const QTreeWidgetItem *item)
    {
        const auto list = item->data(ExemptionColumn, SidsRole).toStringList();
        return QSet<QString>(list.cbegin(), list.cend());
    }
}

AppContainerLoopbackDialog::AppContainerLoopbackDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("AppContainer Loopback Exemptions"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(910, 590);
    setMinimumSize(640, 430);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto description = new QLabel(tr("Allow selected Windows apps to reach a local proxy. Changes are saved immediately in Windows."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto searchRow = new QHBoxLayout;
    search = new QLineEdit(this);
    search->setPlaceholderText(tr("Search apps, packages or SIDs"));
    search->setAccessibleName(tr("Search apps, packages or SIDs"));
    search->setClearButtonEnabled(true);
    filter = new QComboBox(this);
    filter->setObjectName(QStringLiteral("loopbackStatusFilter"));
    filter->setAccessibleName(tr("Exemption status"));
    filter->addItems({ tr("All statuses"), tr("Enabled"), tr("Disabled") });
    refreshButton = new QPushButton(tr("Refresh"), this);
    refreshButton->setToolTip(tr("Detect newly installed apps and reload Windows exemptions."));
    searchRow->addWidget(search, 1);
    searchRow->addWidget(filter);
    searchRow->addWidget(refreshButton);
    layout->addLayout(searchRow);

    tabs = new QTabBar(this);
    tabs->setExpanding(false);
    tabs->addTab(tr("Detected apps"));
    tabs->addTab(tr("Unmatched exemptions"));
    layout->addWidget(tabs);

    tree = new QTreeWidget(this);
    tree->setHeaderLabels({ tr("Application"), tr("Package / AppContainer"), tr("Exemption") });
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree->setAlternatingRowColors(true);
    tree->setUniformRowHeights(true);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->setAccessibleName(tr("AppContainer exemptions"));
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree->header()->resizeSection(0, 250);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(tree, 1);

    summary = new QLabel(this);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    status = new QLabel(this);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status->hide();
    layout->addWidget(status);

    auto actions = new QHBoxLayout;
    enableSelected = new QPushButton(tr("Enable Selected"), this);
    disableSelected = new QPushButton(tr("Disable Selected"), this);
    enableAll = new QPushButton(tr("Enable All"), this);
    disableAll = new QPushButton(tr("Disable All"), this);
    closeButton = new QPushButton(tr("Close"), this);
    enableAll->setToolTip(tr("Enable exemptions for every detected AppContainer, including apps hidden by search or the status filter."));
    disableAll->setToolTip(tr("Disable exemptions for every detected AppContainer. Unmatched exemptions can be disabled separately."));
    for (auto button : { enableSelected, disableSelected, enableAll, disableAll, closeButton, refreshButton })
        button->setAutoDefault(false);
    actions->addWidget(enableSelected);
    actions->addWidget(disableSelected);
    actions->addStretch();
    actions->addWidget(enableAll);
    actions->addWidget(disableAll);
    actions->addWidget(closeButton);
    layout->addLayout(actions);

    connect(search, &QLineEdit::textChanged, this, &AppContainerLoopbackDialog::rebuildList);
    connect(filter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AppContainerLoopbackDialog::rebuildList);
    connect(tabs, &QTabBar::currentChanged, this, &AppContainerLoopbackDialog::rebuildList);
    connect(refreshButton, &QPushButton::clicked, this, &AppContainerLoopbackDialog::refresh);
    connect(closeButton, &QPushButton::clicked, this, &AppContainerLoopbackDialog::reject);
    connect(tree, &QTreeWidget::itemSelectionChanged, this, &AppContainerLoopbackDialog::updateActions);
    connect(tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (column != ExemptionColumn || busy || !stateKnown)
            return;
        const auto targets = ItemSids(item);
        const bool enable = item->checkState(column) == Qt::Checked;
        const auto enabled = (targets & exemptSids).size();
        {
            const QSignalBlocker blocked(tree);
            item->setCheckState(column, enabled == 0 ? Qt::Unchecked : enabled == targets.size() ? Qt::Checked : Qt::PartiallyChecked);
        }
        change(targets, enable);
    });
    connect(enableSelected, &QPushButton::clicked, this, [this] { change(selectedSids(), true); });
    connect(disableSelected, &QPushButton::clicked, this, [this] { change(selectedSids(), false); });
    connect(enableAll, &QPushButton::clicked, this, [this] { change(detectedSids, true, true); });
    connect(disableAll, &QPushButton::clicked, this, [this] { change(detectedSids, false, true); });

    refresh();
}

void AppContainerLoopbackDialog::reject()
{
    if (!busy)
        QDialog::reject();
}

void AppContainerLoopbackDialog::refresh()
{
    run(tr("Reading Windows AppContainers and exemptions…"), [] {
        OperationResult result;
        result.inventory = Loopback::ReadInventory();
        result.exemptions = Loopback::ReadExemptions();
        return result;
    });
}

void AppContainerLoopbackDialog::run(const QString &message, std::function<OperationResult()> operation)
{
    busy = true;
    status->setText(message);
    status->show();
    updateActions();

    const auto result = std::make_shared<OperationResult>();
    auto thread = QThread::create([operation = std::move(operation), result] { *result = operation(); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, result] {
        busy = false;
        QStringList messages;
        if (result->inventory)
        {
            inventory = std::move(*result->inventory);
            detectedSids.clear();
            for (const auto &app : inventory.apps)
                detectedSids.insert(app.sid);
        }
        stateKnown = !result->exemptions.error;
        exemptSids = std::move(result->exemptions.sids);
        if (inventory.error)
            messages << tr("Windows could not detect AppContainers. %1").arg(Loopback::ErrorMessage(inventory.error));
        if (!stateKnown)
            messages << tr("Windows exemption state could not be read. Refresh to try again. %1")
                            .arg(Loopback::ErrorMessage(result->exemptions.error));
        if (result->changeError == UserCancelled)
            messages << tr("Administrator approval was cancelled.");
        else if (result->changeError)
            messages << tr("Windows could not update the exemptions. %1").arg(Loopback::ErrorMessage(result->changeError));
        else if (result->unverified)
            messages << tr("Windows did not report the requested state for %n AppContainer(s).", nullptr, result->unverified);
        else if (!result->inventory && stateKnown)
            messages << tr("Exemptions updated and verified in Windows.");
        status->setText(messages.join(QLatin1Char('\n')));
        status->setVisible(!messages.isEmpty());
        rebuildList();
    });
    thread->start();
}

void AppContainerLoopbackDialog::rebuildList()
{
    const QSignalBlocker blocked(tree);
    QSet<QString> selectedKeys;
    for (const auto item : tree->selectedItems())
        selectedKeys.insert(item->data(0, ItemKeyRole).toString());
    tree->clear();

    const auto query = search->text().trimmed();
    const auto matches = [&](const QString &text) { return text.contains(query, Qt::CaseInsensitive); };
    const auto historical = tabs->currentIndex() == 1;
    const auto unmatched = exemptSids - detectedSids;

    const auto addItem = [&](QTreeWidgetItem *parent, const QString &key, const QString &name, const QString &package,
                             const QString &details, const QStringList &sids) {
        auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
        item->setText(0, name);
        item->setText(1, package);
        item->setToolTip(0, name);
        item->setToolTip(1, details);
        item->setData(0, ItemKeyRole, key);
        item->setData(ExemptionColumn, SidsRole, sids);
        if (stateKnown)
        {
            const auto enabled = std::count_if(sids.cbegin(), sids.cend(), [&](const QString &sid) { return exemptSids.contains(sid); });
            const auto check = enabled == 0 ? Qt::Unchecked : enabled == sids.size() ? Qt::Checked : Qt::PartiallyChecked;
            item->setCheckState(ExemptionColumn, check);
            item->setText(ExemptionColumn, check == Qt::Checked     ? tr("Enabled")
                                           : check == Qt::Unchecked ? tr("Disabled")
                                                                    : tr("%1 of %2 enabled").arg(enabled).arg(sids.size()));
        }
        else
        {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            item->setText(ExemptionColumn, tr("Unknown"));
        }
        item->setSelected(selectedKeys.contains(key));
        return item;
    };

    int visible = 0;
    if (historical)
    {
        for (const auto &sid : unmatched)
        {
            if (!matches(sid))
                continue;
            addItem(nullptr, sid, tr("Unmatched exemption"), sid, sid, { sid });
            ++visible;
        }
    }
    else
    {
        QMap<QString, QVector<const Loopback::AppContainer *>> groups;
        for (const auto &app : inventory.apps)
            groups[app.groupKey].append(&app);
        for (auto group = groups.cbegin(); group != groups.cend(); ++group)
        {
            const auto &members = group.value();
            const auto primary = std::min_element(members.cbegin(), members.cend(), [](const auto a, const auto b) {
                // The parent SID is shorter than its child SIDs. Prefer it for the group title.
                if (a->sid.size() != b->sid.size())
                    return a->sid.size() < b->sid.size();
                return a->sid < b->sid;
            });
            const auto appName = (*primary)->name;
            QTreeWidgetItem *heading = nullptr;
            for (const auto app : members)
            {
                const bool enabled = exemptSids.contains(app->sid);
                if (stateKnown && ((filter->currentIndex() == 1 && !enabled) || (filter->currentIndex() == 2 && enabled)))
                    continue;
                if (!matches(app->name) && !matches(app->containerName) && !matches(app->packageFullName) && !matches(app->sid) &&
                    !matches(group.key()) && !matches(appName))
                    continue;
                if (members.size() > 1 && !heading)
                {
                    QStringList sids;
                    for (const auto member : members)
                        sids.append(member->sid);
                    const auto packageFamily = group.key().startsWith(QStringLiteral("S-1-")) ? QString() : group.key();
                    heading = addItem(nullptr, QStringLiteral("group:") + group.key(), appName, packageFamily,
                                      tr("Selecting or toggling this group includes all %n AppContainer(s) in it.", nullptr, sids.size()), sids);
                    auto font = heading->font(0);
                    font.setBold(true);
                    heading->setFont(0, font);
                    heading->setExpanded(true);
                }

                const auto package = app->packageFullName.isEmpty() ? app->containerName : app->packageFullName;
                const auto name = heading && app->name == appName && !app->containerName.isEmpty() ? app->containerName : app->name;
                QStringList details{ app->sid };
                if (!app->containerName.isEmpty())
                    details.prepend(app->containerName);
                if (!app->packageFullName.isEmpty())
                    details.prepend(app->packageFullName);
                addItem(heading, app->sid, name, package.isEmpty() ? app->sid : package, details.join(QLatin1Char('\n')), { app->sid });
                ++visible;
            }
        }
    }
    tree->sortItems(0, Qt::AscendingOrder);
    tabs->setTabText(0, tr("Detected apps (%1)").arg(detectedSids.size()));
    tabs->setTabText(1, stateKnown ? tr("Unmatched exemptions (%1)").arg(unmatched.size()) : tr("Unmatched exemptions"));

    if (historical)
    {
        if (!stateKnown || unmatched.isEmpty())
            summary->setText(stateKnown ? tr("No unmatched exemptions.") : QString());
        else if (visible == 0)
            summary->setText(tr("No unmatched exemptions match the search."));
        else
            summary->setText(tr("These exemptions have no match in the detected list. Select them to disable them individually or together."));
    }
    else if (!busy && visible == 0)
    {
        summary->setText(query.isEmpty() && filter->currentIndex() == 0 ? tr("No AppContainers detected.") : tr("No apps match the current filter."));
    }
    else
    {
        summary->setText(stateKnown ? tr("%1 shown · %2 detected · %3 enabled").arg(visible).arg(detectedSids.size()).arg((exemptSids & detectedSids).size())
                                    : tr("%1 AppContainer(s) detected").arg(detectedSids.size()));
    }
    updateActions();
}

QSet<QString> AppContainerLoopbackDialog::selectedSids() const
{
    QSet<QString> result;
    for (const auto item : tree->selectedItems())
        result.unite(ItemSids(item));
    return result;
}

void AppContainerLoopbackDialog::updateActions()
{
    const bool historical = tabs->currentIndex() == 1;
    const auto selected = selectedSids();
    const bool canChange = !busy && stateKnown;
    refreshButton->setEnabled(!busy);
    closeButton->setEnabled(!busy);
    search->setEnabled(!busy);
    tabs->setEnabled(!busy);
    tree->setEnabled(!busy);
    filter->setEnabled(canChange && !historical);
    enableSelected->setVisible(!historical);
    enableSelected->setEnabled(canChange && !historical && !(selected - exemptSids).isEmpty());
    disableSelected->setEnabled(canChange && selected.intersects(exemptSids));
    enableAll->setVisible(!historical);
    disableAll->setVisible(!historical);
    enableAll->setEnabled(canChange && !historical && !(detectedSids - exemptSids).isEmpty());
    disableAll->setEnabled(canChange && !historical && detectedSids.intersects(exemptSids));
}

void AppContainerLoopbackDialog::change(const QSet<QString> &targets, bool enable, bool all)
{
    if (busy || !stateKnown || targets.isEmpty())
        return;

    const auto changed = enable ? targets - exemptSids : targets & exemptSids;
    if (changed.isEmpty())
        return;

    if (all || targets.size() > 1)
    {
        QString scope = all ? tr("This applies to all %n detected AppContainer(s), including apps hidden by filters.", nullptr, targets.size())
                            : tr("This applies to %n selected AppContainer(s), including every member of selected groups.", nullptr, targets.size());
        scope += QLatin1String("\n\n") + (enable ? tr("%n exemption(s) would be enabled.", nullptr, changed.size())
                                                  : tr("%n exemption(s) would be disabled.", nullptr, changed.size()));
        if (all)
        {
            scope += QLatin1Char('\n') + tr("Unmatched exemptions are kept.");
            if (enable)
                scope += QLatin1String("\n\n") + tr("Enabling all lets these apps reach services on this PC and reduces their local network isolation.");
        }
        QMessageBox confirm(QMessageBox::Question, enable ? tr("Enable loopback exemptions") : tr("Disable loopback exemptions"),
                            scope, QMessageBox::Yes | QMessageBox::Cancel, this);
        confirm.setTextFormat(Qt::PlainText);
        confirm.setButtonText(QMessageBox::Yes, enable ? tr("Enable") : tr("Disable"));
        confirm.setDefaultButton(QMessageBox::Cancel);
        if (confirm.exec() != QMessageBox::Yes)
            return;
    }

    const auto parentWindow = static_cast<quintptr>(winId());
    run(tr("Applying exemptions… Windows may ask for administrator approval."), [targets, enable, parentWindow] {
        OperationResult result;
        result.changeError = Loopback::ChangeExemptions(targets, enable, parentWindow);
        result.exemptions = Loopback::ReadExemptions();
        if (!result.exemptions.error)
            result.unverified = (enable ? targets - result.exemptions.sids : targets & result.exemptions.sids).size();
        return result;
    });
}
