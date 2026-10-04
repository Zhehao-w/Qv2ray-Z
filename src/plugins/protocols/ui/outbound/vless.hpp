#pragma once

#include "CommonTypes.hpp"
#include "QvGUIPluginInterface.hpp"
#include "base/VLESSSettingsCompatibility.hpp"
#include "ui_vless.h"

class VlessOutboundEditor
    : public Qv2rayPlugin::QvPluginEditor
    , private Ui::vlessOutEditor
{
    Q_OBJECT

  public:
    explicit VlessOutboundEditor(QWidget *parent = nullptr);

    void SetHostAddress(const QString &addr, int port) override
    {
        vless.address = addr;
        vless.port = port;
    }
    QPair<QString, int> GetHostAddress() const override
    {
        return { vless.address, vless.port };
    }

    void SetContent(const QJsonObject &content) override
    {
        this->content = content;
        PLUGIN_EDITOR_LOADING_SCOPE({
            vless = VLESSServerObject::fromJson(Qv2ray::base::vless_settings::ServerForEditing(content));
            if (vless.users.isEmpty())
                vless.users.push_back({});
            originalManagedServer = vless.toJson();
            const auto &user = vless.users.front();
            vLessIDTxt->setText(user.id);
            vLessSecurityCombo->setCurrentText(user.encryption);
            flowCombo->setCurrentText(user.flow);
        })
    }

    const QJsonObject GetContent() const override
    {
        return Qv2ray::base::vless_settings::ApplyManagedServerChanges(content, originalManagedServer, vless.toJson());
    }

  protected:
    void changeEvent(QEvent *e) override;

  private:
    VLESSServerObject vless;
    QJsonObject originalManagedServer;

  private slots:
    void on_flowCombo_currentTextChanged(const QString &arg1);
    void on_vLessIDTxt_textEdited(const QString &arg1);
    void on_vLessSecurityCombo_currentTextChanged(const QString &arg1);
};
