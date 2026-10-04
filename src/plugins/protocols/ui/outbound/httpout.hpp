#pragma once

#include "CommonTypes.hpp"
#include "QvGUIPluginInterface.hpp"
#include "base/SingleServerSettingsCompatibility.hpp"
#include "ui_httpout.h"

class HttpOutboundEditor
    : public Qv2rayPlugin::QvPluginEditor
    , private Ui::httpOutEditor
{
    Q_OBJECT

  public:
    explicit HttpOutboundEditor(QWidget *parent = nullptr);

    void SetHostAddress(const QString &server, int port) override
    {
        http.address = server;
        http.port = port;
    }

    QPair<QString, int> GetHostAddress() const override
    {
        return { http.address, http.port };
    }

    void SetContent(const QJsonObject &source) override
    {
        this->content = source;
        const auto servers = source["servers"].toArray();
        if (!servers.isEmpty())
            http.loadJson(servers.first().toObject());
        PLUGIN_EDITOR_LOADING_SCOPE({
            if (http.users.isEmpty())
                http.users.push_back({});
            originalManagedServer = ManagedServerJson();
            http_UserNameTxt->setText(http.users.first().user);
            http_PasswordTxt->setText(http.users.first().pass);
        })
    }

    const QJsonObject GetContent() const override
    {
        return Qv2ray::base::single_server_settings::ApplyManagedFirstServerChanges(
            content, QStringLiteral("servers"), originalManagedServer, ManagedServerJson(),
            { QStringLiteral("address"), QStringLiteral("port") }, QStringLiteral("users"),
            { QStringLiteral("user"), QStringLiteral("pass") }, true);
    }

  protected:
    void changeEvent(QEvent *e) override;

  private slots:
    void on_http_PasswordTxt_textEdited(const QString &arg1);
    void on_http_UserNameTxt_textEdited(const QString &arg1);

  private:
    QJsonObject ManagedServerJson() const
    {
        auto result = http.toJson();
        if (http.users.isEmpty() || (http.users.first().user.isEmpty() && http.users.first().pass.isEmpty()))
            result.remove(QStringLiteral("users"));
        return result;
    }

    HttpServerObject http;
    QJsonObject originalManagedServer;
};
