#include "w_PluginManager.hpp"

#include "components/plugins/QvPluginHost.hpp"
#include "core/settings/SettingsBackend.hpp"
#include "ui/widgets/editors/w_JsonEditor.hpp"
#include "utils/QvHelpers.hpp"

PluginManageWindow::PluginManageWindow(QWidget *parent) : QvDialog("PluginManager", parent)
{
    addStateOptions("width", { [&] { return width(); }, [&](QJsonValue val) { resize(val.toInt(), size().height()); } });
    addStateOptions("height", { [&] { return height(); }, [&](QJsonValue val) { resize(size().width(), val.toInt()); } });
    addStateOptions("x", { [&] { return x(); }, [&](QJsonValue val) { move(val.toInt(), y()); } });
    addStateOptions("y", { [&] { return y(); }, [&](QJsonValue val) { move(x(), val.toInt()); } });

    setupUi(this);
    setWindowTitle(tr("Bundled Components"));
    openPluginFolder->hide();
    toolButton->hide();

    for (const auto &plugin : PluginHost->AllPlugins())
    {
        const auto &info = PluginHost->GetPlugin(plugin)->metadata;
        auto item = new QListWidgetItem(pluginListWidget);
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        item->setData(Qt::UserRole, info.InternalName);
        item->setText(info.Name + " (" + (PluginHost->GetPlugin(info.InternalName)->isLoaded ? tr("Loaded") : tr("Not loaded")) + ")");
        pluginListWidget->addItem(item);
    }
    pluginListWidget->sortItems();
    isLoading = false;
    if (pluginListWidget->count() > 0)
        on_pluginListWidget_currentItemChanged(pluginListWidget->item(0), nullptr);
}

QvMessageBusSlotImpl(PluginManageWindow){ Q_UNUSED(msg) }

PluginManageWindow::~PluginManageWindow()
{
    on_pluginListWidget_currentItemChanged(nullptr, nullptr);
}

void PluginManageWindow::on_pluginListWidget_currentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous)
    if (currentPluginInfo && currentSettingsWidget)
    {
        currentPluginInfo->pluginInterface->UpdateSettings(currentSettingsWidget->GetSettings());
        pluginSettingsLayout->removeWidget(currentSettingsWidget.get());
        currentSettingsWidget.reset();
    }
    pluginIconLabel->clear();
    if (!current)
        return;

    currentPluginInfo = PluginHost->GetPlugin(current->data(Qt::UserRole).toString());
    auto &info = currentPluginInfo->metadata;

    pluginNameLabel->setText(info.Name);
    pluginAuthorLabel->setText(info.Author);
    pluginDescriptionLabel->setText(info.Description);
    pluginLibPathLabel->setText(currentPluginInfo->libraryPath);
    pluginStateLabel->setText(currentPluginInfo->isLoaded ? tr("Loaded") : tr("Not loaded"));
    pluginComponentsLabel->setText(GetPluginComponentsString(info.Components).join(NEWLINE));

    if (!currentPluginInfo->isLoaded)
    {
        pluginUnloadLabel->setVisible(true);
        pluginUnloadLabel->setText(tr("Bundled component not loaded"));
        return;
    }

    if (currentPluginInfo->hasComponent(COMPONENT_GUI))
    {
        const auto pluginUIInterface = currentPluginInfo->pluginInterface->GetGUIInterface();
        pluginGuiComponentsLabel->setText(GetPluginComponentsString(pluginUIInterface->GetComponents()).join(NEWLINE));
        pluginIconLabel->setPixmap(pluginUIInterface->Icon().pixmap(pluginIconLabel->size() * devicePixelRatio()));
        if (pluginUIInterface->GetComponents().contains(GUI_COMPONENT_SETTINGS))
        {
            currentSettingsWidget = pluginUIInterface->GetSettingsWidget();
            currentSettingsWidget->SetSettings(currentPluginInfo->pluginInterface->GetSettngs());
            pluginUnloadLabel->setVisible(false);
            pluginSettingsLayout->addWidget(currentSettingsWidget.get());
        }
        else
        {
            pluginUnloadLabel->setVisible(true);
            pluginUnloadLabel->setText(tr("Bundled component does not have a settings widget."));
        }
    }
    else
    {
        pluginGuiComponentsLabel->setText(tr("None"));
    }
}

void PluginManageWindow::on_pluginListWidget_itemClicked(QListWidgetItem *item)
{
    Q_UNUSED(item)
}

void PluginManageWindow::on_pluginListWidget_itemChanged(QListWidgetItem *item)
{
    Q_UNUSED(item)
    // Bundled components are required and cannot be enabled or disabled.
}

void PluginManageWindow::on_pluginEditSettingsJsonBtn_clicked()
{
    if (const auto &current = pluginListWidget->currentItem(); current != nullptr)
    {
        const auto &info = PluginHost->GetPlugin(current->data(Qt::UserRole).toString());
        if (!info->isLoaded)
        {
            QvMessageBoxWarn(this, tr("Bundled component not loaded"), tr("This bundled component is not loaded."));
            return;
        }
        JsonEditor w(info->pluginInterface->GetSettngs());
        auto newConf = w.OpenEditor();
        if (w.result() == QDialog::Accepted)
        {
            info->pluginInterface->UpdateSettings(newConf);
        }
    }
}

void PluginManageWindow::on_pluginListWidget_itemSelectionChanged()
{
    const auto hasSelection = !pluginListWidget->selectedItems().isEmpty();
    pluginEditSettingsJsonBtn->setEnabled(hasSelection);
}

void PluginManageWindow::on_openPluginFolder_clicked()
{
    QvMessageBoxInfo(this, tr("External plugins deprecated"), tr("External plugins are no longer supported by Qv2ray-Z."));
}

void PluginManageWindow::on_toolButton_clicked()
{
    QvMessageBoxInfo(this, tr("External plugins deprecated"), tr("External plugins are no longer supported by Qv2ray-Z."));
}
