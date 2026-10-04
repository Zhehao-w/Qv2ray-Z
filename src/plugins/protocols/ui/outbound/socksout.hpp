#pragma once

#include "CommonTypes.hpp"
#include "QvGUIPluginInterface.hpp"
#include "base/SingleServerSettingsCompatibility.hpp"
#include "ui_socksout.h"

class SocksOutboundEditor
    : public Qv2rayPlugin::QvPluginEditor
    , private Ui::socksOutEditor
{
    Q_OBJECT

  public:
    explicit SocksOutboundEditor(QWidget *parent = nullptr);

    void SetHostAddress(const QString &server, int port) override
    {
        socks.address = server;
        socks.port = port;
    }

    QPair<QString, int> GetHostAddress() const override
    {
        return { socks.address, socks.port };
    }

    void SetContent(const QJsonObject &source) override
    {
        this->content = source;
        const auto servers = source["servers"].toArray();
        if (!servers.isEmpty())
            socks.loadJson(servers.first().toObject());
        PLUGIN_EDITOR_LOADING_SCOPE({
            if (socks.users.isEmpty())
                socks.users.push_back({});
            originalManagedServer = socks.toJson();
            socks_UserNameTxt->setText(socks.users.first().user);
            socks_PasswordTxt->setText(socks.users.first().pass);
        })
    }

    const QJsonObject GetContent() const override
    {
        return Qv2ray::base::single_server_settings::ApplyManagedFirstServerChanges(
            content, QStringLiteral("servers"), originalManagedServer, socks.toJson(),
            { QStringLiteral("address"), QStringLiteral("port") }, QStringLiteral("users"),
            { QStringLiteral("user"), QStringLiteral("pass") }, true);
    }

  protected:
    void changeEvent(QEvent *e) override;

  private slots:
    void on_socks_UserNameTxt_textEdited(const QString &arg1);
    void on_socks_PasswordTxt_textEdited(const QString &arg1);

  private:
    SocksServerObject socks;
    QJsonObject originalManagedServer;
};
