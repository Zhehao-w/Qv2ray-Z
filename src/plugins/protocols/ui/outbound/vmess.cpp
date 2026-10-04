#include "vmess.hpp"

#include "BuiltinProtocolPlugin.hpp"

VmessOutboundEditor::VmessOutboundEditor(QWidget *parent) : Qv2rayPlugin::QvPluginEditor(parent)
{
    setupUi(this);
    setProperty("QV2RAY_INTERNAL_HAS_STREAMSETTINGS", true);
    setProperty("QV2RAY_INTERNAL_HAS_FORWARD_PROXY", true);
}

void VmessOutboundEditor::changeEvent(QEvent *e)
{
    QWidget::changeEvent(e);
    switch (e->type())
    {
        case QEvent::LanguageChange: retranslateUi(this); break;
        default: break;
    }
}

void VmessOutboundEditor::SetContent(const QJsonObject &content)
{
    this->content = content;
    PLUGIN_EDITOR_LOADING_SCOPE({
        auto editableContent = content;
        if (editableContent["vnext"].toArray().isEmpty())
            editableContent["vnext"] = QJsonArray{ QJsonObject{} };
        vmess = VMessServerObject::fromJson(editableContent["vnext"].toArray().first().toObject());
        if (vmess.users.empty())
            vmess.users.push_back({});
        originalManagedServer = vmess.toJson();
        const auto &user = vmess.users.front();
        idLineEdit->setText(user.id);
        alterLineEdit->setValue(user.alterId);

        securityCombo->setCurrentText(user.security);
    })

    if (alterLineEdit->value() > 0)
    {
        const auto msg = tr("VMess MD5 with Non-zero AlterID has been deprecated, please use VMessAEAD.");
        InternalProtocolSupportPluginInstance->PluginErrorMessageBox(tr("Non AEAD VMess detected"), msg);
    }
}

void VmessOutboundEditor::on_idLineEdit_textEdited(const QString &arg1)
{
    const static QRegularExpression regExpUUID("^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$", QRegularExpression::CaseInsensitiveOption);

    if (!regExpUUID.match(arg1).hasMatch())
    {
        RED(idLineEdit)
    }
    else
    {
        BLACK(idLineEdit)
    }
    if (vmess.users.isEmpty())
        vmess.users << VMessServerObject::UserObject{};
    vmess.users.front().id = arg1;
}

void VmessOutboundEditor::on_securityCombo_currentTextChanged(const QString &arg1)
{
    if (vmess.users.isEmpty())
        vmess.users << VMessServerObject::UserObject{};
    vmess.users.front().security = arg1;
}

void VmessOutboundEditor::on_alterLineEdit_valueChanged(int arg1)
{
    if (vmess.users.isEmpty())
        vmess.users << VMessServerObject::UserObject{};
    vmess.users.front().alterId = arg1;
}
