#include "w_OutboundEditor.hpp"

#include "core/connection/Generation.hpp"
#include "core/connection/OutboundEditorPersistence.hpp"
#include "core/connection/TlsPinCompatibility.hpp"
#include "plugin-interface/QvGUIPluginInterface.hpp"
#include "ui/widgets/common/WidgetUIBase.hpp"
#include "ui/widgets/editors/w_JsonEditor.hpp"
#include "ui/widgets/editors/w_RoutesEditor.hpp"

#include <QFile>
#include <QIntValidator>
#include <QPushButton>

#define QV_MODULE_NAME "OutboundEditor"

OutboundEditor::OutboundEditor(QWidget *parent) : QDialog(parent), tag(OUTBOUND_TAG_PROXY)
{
    QvMessageBusConnect(OutboundEditor);
    setupUi(this);
    nameTxt = new QLineEdit(this);
    nameTxt->setPlaceholderText(tr("A clear name shown in the connection list"));
    formLayout_5->insertRow(0, tr("Name"), nameTxt);
    ipLabel_3->setText(tr("Xray Outbound Tag"));
    tagTxt->setPlaceholderText(tr("Internal routing identifier (most users should not change this)"));
    tagTxt->setToolTip(tr("Used internally by Xray routing. Most users should not need to change this."));
    //
    streamSettingsWidget = new StreamSettingsWidget(this);
    streamSettingsWidget->SetStreamObject({});
    if (auto *pinButton = streamSettingsWidget->findChild<QPushButton *>(QStringLiteral("pinnedPeerCertificateChainSha256Btn")))
    {
        pinButton->setText(tr("Open pinnedPeerCertSha256 Editor"));
        pinButton->setToolTip(
            tr("Edits the current Xray pinnedPeerCertSha256 value. Legacy pinnedPeerCertificateChainSha256 data is preserved unchanged."));
    }
    transportFrame->addWidget(streamSettingsWidget);
    //
    for (const auto &name : PluginHost->UsablePlugins())
    {
        const auto &plugin = PluginHost->GetPlugin(name);
        if (!plugin->hasComponent(COMPONENT_GUI))
            continue;

        const auto guiInterface = plugin->pluginInterface->GetGUIInterface();

        if (!guiInterface)
            LOG("Found a plugin with COMPONENT_GUI but returns an invalid GUI interface: " + plugin->metadata.Name);

        if (!guiInterface->GetComponents().contains(GUI_COMPONENT_OUTBOUND_EDITOR))
            continue;

        const auto editors = guiInterface->GetOutboundEditors();
        for (const auto &editorInfo : editors)
        {
            outBoundTypeCombo->addItem(editorInfo.first.displayName, editorInfo.first.protocol);
            outboundTypeStackView->addWidget(editorInfo.second);
            pluginWidgets.insert(editorInfo.first.protocol, editorInfo.second);
        }
    }
    outBoundTypeCombo->model()->sort(0);
    useForwardProxy = false;
}

QvMessageBusSlotImpl(OutboundEditor)
{
    switch (msg)
    {
        MBShowDefaultImpl;
        MBHideDefaultImpl;
        MBRetranslateDefaultImpl;
        case UPDATE_COLORSCHEME: break;
    }
}

OutboundEditor::OutboundEditor(const OUTBOUND &outboundEntry, QWidget *parent, const QString &displayName) : OutboundEditor(parent)
{
    originalConfig = outboundEntry;
    nameTxt->setText(displayName);
    reloadGUI();
}

OutboundEditor::~OutboundEditor()
{
}

void OutboundEditor::accept()
{
    const auto candidate = generateConnectionJson();
    // Validate after restoring opaque TLS fields so a supported alternate
    // verifier can satisfy the same safety rule used by the launch boundary.
    if (const auto error = Qv2ray::core::connection::tls_pin::ValidateRuntimeTlsPins(QJsonObject{ { "outbounds", QJsonArray{ candidate } } }); error)
    {
        QvMessageBoxWarn(this, tr("Invalid TLS certificate pin settings"), *error);
        return;
    }

    resultConfig = candidate;
    QDialog::accept();
}

OUTBOUND OutboundEditor::OpenEditor()
{
    int resultCode = this->exec();
    return resultCode == QDialog::Accepted ? resultConfig : originalConfig;
}

QString OutboundEditor::GetFriendlyName()
{
    auto host = ipLineEdit->text().replace(":", "-").replace("/", "_").replace("\\", "_");
    auto port = portLineEdit->text().replace(":", "-").replace("/", "_").replace("\\", "_");
    return nameTxt->text().trimmed().isEmpty() ? outboundType + "@" + host + ":" + port : nameTxt->text().trimmed();
}

QString OutboundEditor::GetDisplayName() const
{
    return nameTxt->text().trimmed();
}

OUTBOUND OutboundEditor::generateConnectionJson()
{
    OUTBOUNDSETTING settings;
    auto streaming = streamSettingsWidget->GetStreamSettings().toJson();
    Qv2ray::core::connection::tls_pin::FinalizeTlsPinForXray(originalConfig.value("streamSettings").toObject(), streaming);
    bool processed = false;
    for (const auto &[protocol, widget] : pluginWidgets.toStdMap())
    {
        if (protocol == outboundType)
        {
            widget->SetHostAddress(serverAddress, serverPort);
            settings = OUTBOUNDSETTING(widget->GetContent());
            const auto prop = widget->property("");
            const auto hasStreamSettings = GetProperty(widget, "QV2RAY_INTERNAL_HAS_STREAMSETTINGS");
            if (!hasStreamSettings)
                streaming = {};
            processed = true;
            break;
        }
    }
    if (!processed)
    {
        QvMessageBoxWarn(this, tr("Unknown outbound type."),
                         tr("The specified outbound type is not supported, this may happen due to a plugin failure."));
    }
    auto root = GenerateOutboundEntry(tag, outboundType, settings, streaming, muxConfig);
    root[QV2RAY_USE_FPROXY_KEY] = useForwardProxy;
    return PreserveUneditedOutboundFields(originalConfig, root);
}

void OutboundEditor::reloadGUI()
{
    if (originalConfig.contains("tag"))
        tag = originalConfig["tag"].toString();
    tagTxt->setText(tag);
    outboundType = originalConfig["protocol"].toString("vmess");
    muxConfig = originalConfig.contains("mux") ? originalConfig["mux"].toObject() : QJsonObject{};
    useForwardProxy = originalConfig[QV2RAY_USE_FPROXY_KEY].toBool(false);
    const auto originalStream = originalConfig["streamSettings"].toObject();
    auto streamObject = StreamSettingsObject::fromJson(originalStream);
    Qv2ray::core::connection::tls_pin::PrepareTlsPinEditorModel(originalStream, streamObject);
    streamSettingsWidget->SetStreamObject(streamObject);
    //
    useFPCB->setChecked(useForwardProxy);
    muxEnabledCB->setChecked(muxConfig["enabled"].toBool());
    muxConcurrencyTxt->setValue(muxConfig["concurrency"].toInt());
    //
    const auto &settings = originalConfig["settings"].toObject();
    //
    bool processed = false;
    for (const auto &[protocol, widget] : pluginWidgets.toStdMap())
    {
        if (protocol == outboundType)
        {
            outBoundTypeCombo->setCurrentIndex(outBoundTypeCombo->findData(protocol));
            widget->SetContent(settings);
            const auto &[_address, _port] = widget->GetHostAddress();
            serverAddress = _address;
            serverPort = _port;
            ipLineEdit->setText(_address);
            portLineEdit->setText(QSTRN(_port));
            processed = true;
            break;
        }
    }
    if (!processed)
    {
        LOG("Outbound type: ", outboundType, " is not supported.");
        QvMessageBoxWarn(this, tr("Unknown outbound."),
                         tr("The specified outbound type is invalid, this may be caused by a plugin failure.") + NEWLINE +
                             tr("Please use the JsonEditor or reload the plugin."));
        reject();
    }
}

void OutboundEditor::on_ipLineEdit_textEdited(const QString &arg1)
{
    serverAddress = arg1;
}

void OutboundEditor::on_portLineEdit_textEdited(const QString &arg1)
{
    serverPort = arg1.toInt();
}

void OutboundEditor::on_tagTxt_textEdited(const QString &arg1)
{
    tag = arg1;
}

void OutboundEditor::on_muxEnabledCB_stateChanged(int arg1)
{
    muxConfig["enabled"] = arg1 == Qt::Checked;
}

void OutboundEditor::on_muxConcurrencyTxt_valueChanged(int arg1)
{
    muxConfig["concurrency"] = arg1;
}

void OutboundEditor::on_useFPCB_stateChanged(int arg1)
{
    useForwardProxy = arg1 == Qt::Checked;
    streamSettingsWidget->setEnabled(!useForwardProxy);
}

void OutboundEditor::on_outBoundTypeCombo_currentIndexChanged(int)
{
    outboundType = outBoundTypeCombo->currentData().toString();
    auto newWidget = pluginWidgets[outboundType];
    if (!newWidget)
        return;
    outboundTypeStackView->setCurrentWidget(newWidget);
    const auto hasStreamSettings = GetProperty(newWidget, "QV2RAY_INTERNAL_HAS_STREAMSETTINGS");
    const auto hasForwardProxy = GetProperty(newWidget, "QV2RAY_INTERNAL_HAS_FORWARD_PROXY");
    streamSettingsWidget->setEnabled(hasStreamSettings);
    useFPCB->setEnabled(hasForwardProxy);
    if (!hasForwardProxy)
        useFPCB->setToolTip(tr("Forward proxy has been disabled when using plugin outbound"));
}
