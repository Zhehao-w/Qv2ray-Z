#pragma once

#include "CommonTypes.hpp"
#include "QvGUIPluginInterface.hpp"
#include "base/SingleServerSettingsCompatibility.hpp"
#include "ui_vmess.h"

class VmessOutboundEditor
    : public Qv2rayPlugin::QvPluginEditor
    , private Ui::vmessOutEditor
{
    Q_OBJECT

  public:
    explicit VmessOutboundEditor(QWidget *parent = nullptr);

    void SetHostAddress(const QString &addr, int port) override
    {
        vmess.address = addr;
        vmess.port = port;
    }

    QPair<QString, int> GetHostAddress() const override
    {
        return { vmess.address, vmess.port };
    }

    void SetContent(const QJsonObject &content) override;
    const QJsonObject GetContent() const override
    {
        return Qv2ray::base::single_server_settings::ApplyManagedFirstServerChanges(
            content, QStringLiteral("vnext"), originalManagedServer, vmess.toJson(),
            { QStringLiteral("address"), QStringLiteral("port") }, QStringLiteral("users"),
            { QStringLiteral("id"), QStringLiteral("alterId"), QStringLiteral("security") });
    }

  private:
    VMessServerObject vmess;
    QJsonObject originalManagedServer;

  protected:
    void changeEvent(QEvent *e) override;

  private slots:
    void on_idLineEdit_textEdited(const QString &arg1);
    void on_securityCombo_currentTextChanged(const QString &arg1);
    void on_alterLineEdit_valueChanged(int arg1);
};
