#pragma once

#include "CommonTypes.hpp"
#include "QvGUIPluginInterface.hpp"
#include "base/SingleServerSettingsCompatibility.hpp"
#include "ui_shadowsocks.h"

class ShadowsocksOutboundEditor
    : public Qv2rayPlugin::QvPluginEditor
    , private Ui::shadowsocksOutEditor
{
    Q_OBJECT

  public:
    explicit ShadowsocksOutboundEditor(QWidget *parent = nullptr);

    void SetHostAddress(const QString &addr, int port) override
    {
        shadowsocks.address = addr;
        shadowsocks.port = port;
    };
    QPair<QString, int> GetHostAddress() const override
    {
        return { shadowsocks.address, shadowsocks.port };
    };

    void SetContent(const QJsonObject &content) override
    {
        this->content = content;
        PLUGIN_EDITOR_LOADING_SCOPE({
            const auto servers = content["servers"].toArray();
            shadowsocks = ShadowSocksServerObject::fromJson(servers.isEmpty() ? QJsonObject{} : servers.first().toObject());
            originalManagedServer = shadowsocks.toJson();
            ss_passwordTxt->setText(shadowsocks.password);
            ss_encryptionMethod->setCurrentText(shadowsocks.method);
        })
    }
    const QJsonObject GetContent() const override
    {
        return Qv2ray::base::single_server_settings::ApplyManagedFirstServerChanges(
            content, QStringLiteral("servers"), originalManagedServer, shadowsocks.toJson(),
            { QStringLiteral("address"), QStringLiteral("port"), QStringLiteral("method"), QStringLiteral("password") });
    }

  protected:
    void changeEvent(QEvent *e) override;

  private slots:
    void on_ss_encryptionMethod_currentTextChanged(const QString &arg1);
    void on_ss_passwordTxt_textEdited(const QString &arg1);

  private:
    ShadowSocksServerObject shadowsocks;
    QJsonObject originalManagedServer;
};
