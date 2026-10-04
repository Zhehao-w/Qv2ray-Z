#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "core/handler/ConfigHandler.hpp"
#include "core/handler/KernelInstanceHandler.hpp"
#include "core/handler/RouteHandler.hpp"
#include "ui/widgets/Qv2rayWidgetApplication.hpp"
#include "ui/widgets/editors/w_RoutesEditor.hpp"
#include "ui/widgets/styles/StyleManager.hpp"
#include "ui/widgets/widgets/DnsSettingsWidget.hpp"
#include "ui/widgets/windows/w_GroupManager.hpp"
#include "utils/QvHelpers.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QTest>

namespace
{
    class RoutingTestApplication : public Qv2rayWidgetApplication
    {
      public:
        using Qv2rayWidgetApplication::Qv2rayWidgetApplication;
        int warnings = 0;
        QString warning;
        bool modalWarnings = false;
        MessageOpt answer = Yes;
        void MessageBoxWarn(QWidget *parent, const QString &title, const QString &text) override
        {
            ++warnings;
            warning = text;
            if (modalWarnings)
            {
                QTimer::singleShot(0,
                                   []
                                   {
                                       for (auto *widget : QApplication::topLevelWidgets())
                                           if (auto *box = qobject_cast<QMessageBox *>(widget))
                                               box->accept();
                                   });
                Qv2rayWidgetApplication::MessageBoxWarn(parent, title, text);
            }
        }
        MessageOpt MessageBoxAsk(QWidget *, const QString &, const QString &, const QList<MessageOpt> &) override
        {
            return answer;
        }
    };

    RoutingTestApplication *application;

    struct Fixture
    {
        QTemporaryDir directory;
        explicit Fixture(const QByteArray &routes = {}, bool directoryInsteadOfFile = false)
        {
            REQUIRE(directory.isValid());
            delete application->ConfigObject;
            application->ConfigObject = new Qv2rayConfigObject;
            GlobalConfig.uiConfig.quietMode = true;
            GlobalConfig.kernelConfig.enableAPI = false;
            GlobalConfig.inboundConfig.systemProxySettings.setSystemProxy = false;
            application->StartupArguments = {};
            application->ConfigPath = directory.path() + "/";
            application->warnings = 0;
            application->warning.clear();
            application->modalWarnings = false;
            application->answer = Yes;
            if (directoryInsteadOfFile)
                REQUIRE(QDir().mkpath(application->ConfigPath + "routes.json"));
            else if (!routes.isEmpty())
                REQUIRE(StringToFile(QString::fromUtf8(routes), application->ConfigPath + "routes.json"));
            PluginHost = new QvPluginHost;
            RouteManager = new RouteHandler;
            ConnectionManager = new QvConfigHandler;
        }
        ~Fixture()
        {
            delete ConnectionManager;
            ConnectionManager = nullptr;
            KernelInstance = nullptr;
            delete RouteManager;
            RouteManager = nullptr;
            delete PluginHost;
            PluginHost = nullptr;
            application->processEvents();
        }
    };

    CONFIGROOT ComplexRoot()
    {
        return CONFIGROOT(QJsonObject{
            { "outbounds", QJsonArray{ QJsonObject{ { "protocol", "freedom" }, { "tag", "direct" } } } },
            { "routing",
              QJsonObject{
                  { "rules", QJsonArray{ QJsonObject{ { "type", "field" }, { "ip", QJsonArray{ "0.0.0.0/0" } }, { "outboundTag", "direct" } } } } } },
            { "dns",
              QJsonObject{ { "hosts", QJsonObject{ { "array.example", QJsonArray{ "1.1.1.1", "8.8.8.8" } }, { "string.example", "127.0.0.1" } } },
                           { "servers", QJsonArray{ QJsonObject{ { "address", "1.1.1.1" },
                                                                 { "domains", QJsonArray{ "domain:one.example" } },
                                                                 { "skipFallback", true },
                                                                 { "futureServer", "first" } },
                                                    QJsonObject{ { "address", "1.1.1.1" },
                                                                 { "domains", QJsonArray{ "domain:two.example" } },
                                                                 { "futureServer", "second" } },
                                                    "8.8.8.8" } },
                           { "disableFallbackIfMatch", true },
                           { "futureDns", QJsonObject{ { "keep", true } } } } },
            { "observatory", QJsonObject{ { "subjectSelector", QJsonArray{ "direct" } },
                                          { "probeURL", "https://probe.example/" },
                                          { "probeInterval", "1m" },
                                          { "enableConcurrency", true } } },
            { "browserForwarder", QJsonObject{ { "listenAddr", "127.0.0.1" }, { "listenPort", 12345 }, { "future", true } } },
            { "fakedns", QJsonArray{ QJsonObject{ { "ipPool", "198.18.0.0/15" }, { "poolSize", 65535 } } } },
            { "futureRoot", true } });
    }

    CONFIGROOT Accept(RouteEditor &editor)
    {
        QTimer::singleShot(0, &editor, &QDialog::accept);
        return editor.OpenEditor();
    }

    template<typename T>
    T *Child(QObject &object, const char *name)
    {
        auto *widget = object.findChild<T *>(name);
        REQUIRE(widget);
        return widget;
    }

    void EditLine(QLineEdit *line, const QString &text)
    {
        line->setText(text);
        REQUIRE(QMetaObject::invokeMethod(line, "textEdited", Qt::DirectConnection, Q_ARG(QString, text)));
    }
} // namespace

int main(int argc, char **argv)
{
    RoutingTestApplication app(argc, argv);
    application = &app;
    // The shipped application installs Fusion and the built-in stylesheet
    // before showing dialogs. Exercise the same initialization in these tests.
    QvStyleManager style;
    style.ApplyStyle();
    return Catch::Session().run(argc, argv);
}

TEST_CASE("Actual routing dialog preserves DNS and auxiliary raw JSON on no-edit save")
{
    Fixture fixture;
    const auto before = ComplexRoot();
    RouteEditor editor(before);
    const auto after = Accept(editor);
    for (const auto &key : { "dns", "fakedns", "observatory", "browserForwarder", "futureRoot" })
    {
        INFO(key);
        REQUIRE(after.value(key) == before.value(key));
    }
    REQUIRE(application->warnings == 0);
}

TEST_CASE("Route DNS edits patch managed fields without flattening opaque server or host values")
{
    Fixture fixture;
    const auto before = ComplexRoot();
    RouteEditor editor(before);
    auto *dns = Child<DnsSettingsWidget>(editor, "DnsSettingsWidget");
    EditLine(Child<QLineEdit>(*dns, "dnsTagTxt"), "edited-tag");
    EditLine(Child<QLineEdit>(*dns, "serverAddressTxt"), "9.9.9.9");
    REQUIRE(Child<QSpinBox>(*dns, "serverPortSB")->isEnabled());
    Child<QSpinBox>(*dns, "serverPortSB")->setValue(5353);
    auto expected = before.value("dns").toObject();
    auto servers = expected.value("servers").toArray();
    auto first = servers[0].toObject();
    first["address"] = "9.9.9.9";
    first["port"] = 5353;
    servers[0] = first;
    expected["servers"] = servers;
    expected["tag"] = "edited-tag";
    REQUIRE(Accept(editor).value("dns") == expected);
}

TEST_CASE("Raw DNS server identity survives duplicate addresses reorder remove and add")
{
    Fixture fixture;
    RouteEditor editor(ComplexRoot());
    auto *dns = editor.findChild<DnsSettingsWidget *>();
    REQUIRE(dns);
    REQUIRE(QMetaObject::invokeMethod(dns, "on_moveServerDownBtn_clicked", Qt::DirectConnection));
    auto result = dns->GetDNSJson().toObject().value("servers").toArray();
    REQUIRE(result[0].toObject().value("futureServer") == "second");
    REQUIRE(result[1].toObject().value("futureServer") == "first");
    REQUIRE(QMetaObject::invokeMethod(dns, "on_removeServerBtn_clicked", Qt::DirectConnection));
    REQUIRE(QMetaObject::invokeMethod(dns, "on_addServerBtn_clicked", Qt::DirectConnection));
    result = Accept(editor).value("dns").toObject().value("servers").toArray();
    REQUIRE(result.size() == 3);
    REQUIRE(result[0].toObject().value("futureServer") == "second");
    REQUIRE(result[1] == "8.8.8.8");
    REQUIRE(result[2] == "1.1.1.1");
}

TEST_CASE("Editing DNS host strings retains read-only array values")
{
    Fixture fixture;
    const auto before = ComplexRoot();
    RouteEditor editor(before);
    auto *table = Child<QTableWidget>(editor, "staticResolvedDomainsTable");
    REQUIRE(table->rowCount() == 2);
    REQUIRE_FALSE(table->item(0, 1)->flags().testFlag(Qt::ItemIsEditable));
    REQUIRE(table->item(0, 1)->text().contains("8.8.8.8"));
    table->item(1, 1)->setText("127.0.0.2");
    const auto hosts = Accept(editor).value("dns").toObject().value("hosts").toObject();
    REQUIRE(hosts.value("array.example") == before.value("dns").toObject().value("hosts").toObject().value("array.example"));
    REQUIRE(hosts.value("string.example") == "127.0.0.2");
}

TEST_CASE("Observatory browser forwarder and FakeDNS edits preserve unrelated fields")
{
    Fixture fixture;
    auto before = ComplexRoot();
    before["fakedns"] = QJsonObject{ { "ipPool", "198.18.0.0/15" }, { "poolSize", 65535 }, { "future", "keep" } };
    RouteEditor editor(before);
    Child<QPlainTextEdit>(editor, "obSubjectSelectorTxt")->setPlainText("direct\nsecond");
    Child<QSpinBox>(editor, "bfListenPortTxt")->setValue(12346);
    Child<QSpinBox>(editor, "fakeDNSIPPoolSize")->setValue(65534);
    const auto result = Accept(editor);
    auto observatory = before.value("observatory").toObject();
    observatory["subjectSelector"] = QJsonArray{ "direct", "second" };
    REQUIRE(result.value("observatory") == observatory);
    auto forwarder = before.value("browserForwarder").toObject();
    forwarder["listenPort"] = 12346;
    REQUIRE(result.value("browserForwarder") == forwarder);
    auto fake = before.value("fakedns").toObject();
    fake["poolSize"] = 65534;
    REQUIRE(result.value("fakedns") == fake);
    REQUIRE(result.value("dns") == before.value("dns"));
}

TEST_CASE("Absent and unrepresentable auxiliary roots survive a no-edit routing save")
{
    Fixture fixture;
    auto before = ComplexRoot();
    SECTION("absent")
    {
        for (const auto &key : { "dns", "fakedns", "observatory", "browserForwarder" })
            before.remove(key);
    }
    SECTION("raw unsupported shapes")
    {
        before["dns"] = QJsonArray{ "opaque" };
        before["observatory"] = "opaque";
        before["browserForwarder"] = QJsonArray{ "opaque" };
    }
    RouteEditor editor(before);
    const auto after = Accept(editor);
    for (const auto &key : { "dns", "fakedns", "observatory", "browserForwarder" })
    {
        INFO(key);
        REQUIRE(after.value(key) == before.value(key));
    }
}

TEST_CASE("Replacing DNS models clears stale rows and does not rewrite server details during load")
{
    Fixture fixture;
    DnsSettingsWidget widget;
    auto model = DNSObject::fromJson(ComplexRoot().value("dns").toObject());
    model.servers[0].QV2RAY_DNS_IS_COMPLEX_DNS = true;
    model.servers[0].port = 5353;
    widget.SetDNSObject(model, {});
    REQUIRE(widget.GetDNSObject().first == model);
    widget.SetDNSObject({}, {});
    REQUIRE(Child<QTableWidget>(widget, "staticResolvedDomainsTable")->rowCount() == 0);
    REQUIRE(widget.GetDNSObject().first.hosts.isEmpty());
    widget.SetDNSObject(model, {});
    REQUIRE(widget.GetDNSObject().first == model);
}

TEST_CASE("Invalid or unreadable route storage blocks dependent startup before kernel dispatch")
{
    const auto unreadable = GENERATE(false, true);
    Fixture fixture("{broken routes", unreadable);
    CONFIGROOT root(QJsonObject{ { "outbounds", QJsonArray{ QJsonObject{ { "protocol", "freedom" }, { "tag", "direct" } } } } });
    const auto pair = ConnectionManager->CreateConnection(root, "blocked");
    REQUIRE_FALSE(pair.isEmpty());
    REQUIRE(RouteManager->GetRuntimeConfigError(root).has_value());
    REQUIRE(RouteManager->GenerateFinalConfig(root, ConnectionManager->GetGroupRoutingId(pair.groupId)).isEmpty());
    const auto meta = ConnectionManager->GetConnectionMetaObject(pair.connectionId).toJson();
    REQUIRE(StringToFile("existing-generated-config", application->ConfigPath + "config.gen.json"));
    REQUIRE_FALSE(ConnectionManager->StartConnection(pair));
    REQUIRE(ConnectionManager->CurrentConnection().isEmpty());
    REQUIRE(application->warnings == 1);
    REQUIRE(application->warning.contains("routes.json"));
    REQUIRE(ConnectionManager->GetConnectionMetaObject(pair.connectionId).toJson() == meta);
    REQUIRE(ConnectionManager->GetConnectionRoot(pair.connectionId) == root);
    REQUIRE(StringFromFile(application->ConfigPath + "config.gen.json") == "existing-generated-config");
    REQUIRE_FALSE(RouteManager->SaveRoutes());
    if (unreadable)
        REQUIRE(QFileInfo(application->ConfigPath + "routes.json").isDir());
    else
        REQUIRE(StringFromFile(application->ConfigPath + "routes.json") == "{broken routes");
}

TEST_CASE("Missing and valid route storage permit ordinary runtime generation")
{
    const auto routes = GENERATE(QByteArray{}, QByteArray("{}"));
    Fixture fixture(routes);
    CONFIGROOT root(QJsonObject{ { "outbounds", QJsonArray{ QJsonObject{ { "protocol", "freedom" }, { "tag", "direct" } } } } });
    REQUIRE_FALSE(RouteManager->GetRuntimeConfigError(root).has_value());
    REQUIRE_FALSE(RouteManager->GenerateFinalConfig(root, GroupRoutingId{ "new-group" }).isEmpty());
}

TEST_CASE("Self-contained complex configs do not need damaged group routes but missing DNS still does")
{
    Fixture fixture("{broken");
    auto root = ComplexRoot();
    REQUIRE_FALSE(RouteManager->GetRuntimeConfigError(root).has_value());
    REQUIRE_FALSE(RouteManager->GenerateFinalConfig(root, GroupRoutingId{ "test" }).isEmpty());
    root.remove("dns");
    REQUIRE(RouteManager->GetRuntimeConfigError(root).has_value());
    REQUIRE(RouteManager->GenerateFinalConfig(root, GroupRoutingId{ "test" }).isEmpty());
}

TEST_CASE("Failed modal group save keeps selected group form and subsequent writes consistent")
{
    const auto mouse = GENERATE(true, false);
    INFO(mouse);
    Fixture fixture("{broken");
    const auto second = ConnectionManager->CreateGroup("second", false);
    REQUIRE(second != NullGroupId);
    INFO("Creating the group dialog");
    GroupManager manager;
    INFO("Group dialog constructed; showing it");
    manager.show();
    INFO("Group dialog shown; processing pending events");
    application->processEvents();
    auto *list = Child<QListWidget>(manager, "groupList");
    auto *previous = list->currentItem();
    const auto previousId = GroupId{ previous->data(Qt::UserRole).toString() };
    QListWidgetItem *target = nullptr;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i) != previous)
            target = list->item(i);
    REQUIRE(target);
    application->modalWarnings = true;
    INFO("Sending the group selection input");
    if (mouse)
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(target).center());
    else
        list->setCurrentItem(target);
    application->processEvents();
    REQUIRE(application->warnings == 1);
    REQUIRE(list->currentItem() == previous);
    REQUIRE(list->selectedItems() == QList<QListWidgetItem *>{ previous });
    REQUIRE(Child<QLineEdit>(manager, "groupNameTxt")->text() == GetDisplayName(previousId));
    EditLine(Child<QLineEdit>(manager, "groupNameTxt"), "correct-group");
    REQUIRE(GetDisplayName(previousId) == "correct-group");
    REQUIRE(GetDisplayName(GroupId{ target->data(Qt::UserRole).toString() }) != "correct-group");
    REQUIRE(StringFromFile(application->ConfigPath + "routes.json") == "{broken");
}

TEST_CASE("Successful mouse group switch loads once and clears old DNS host rows")
{
    Fixture fixture;
    const auto second = ConnectionManager->CreateGroup("second", false);
    REQUIRE(second != NullGroupId);
    INFO("Creating the group dialog");
    GroupManager manager;
    INFO("Group dialog constructed; showing it");
    manager.show();
    INFO("Group dialog shown; processing pending events");
    application->processEvents();
    auto *list = Child<QListWidget>(manager, "groupList");
    auto *previous = list->currentItem();
    const auto previousId = GroupId{ previous->data(Qt::UserRole).toString() };
    DNSObject dns;
    dns.hosts["old.example"] = "127.0.0.1";
    auto *dnsWidget = manager.findChild<DnsSettingsWidget *>();
    REQUIRE(dnsWidget);
    dnsWidget->SetDNSObject(dns, {});
    QListWidgetItem *target = nullptr;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i) != previous)
            target = list->item(i);
    REQUIRE(target);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(target).center());
    REQUIRE(application->warnings == 0);
    REQUIRE(list->currentItem() == target);
    REQUIRE(Child<QLineEdit>(manager, "groupNameTxt")->text() == target->text());
    REQUIRE(dnsWidget->GetDNSObject().first.hosts.isEmpty());
    REQUIRE(Child<QTableWidget>(manager, "staticResolvedDomainsTable")->rowCount() == 0);
    REQUIRE(std::get<1>(RouteManager->GetDNSSettings(ConnectionManager->GetGroupRoutingId(previousId))).hosts == dns.hosts);
    REQUIRE(QFile::exists(application->ConfigPath + "routes.json"));
}

TEST_CASE("Group route persistence retains unknown nested settings on no-op and scalar edits")
{
    const auto edit = GENERATE(false, true);
    const QJsonObject original{
        { "overrideDNS", true },
        { "dnsConfig", QJsonObject{ { "hosts", QJsonObject{ { "array.example", QJsonArray{ "1.1.1.1", "8.8.8.8" } } } },
                                    { "servers", QJsonArray{ QJsonObject{ { "address", "1.1.1.1" }, { "skipFallback", true } } } },
                                    { "futureDns", true } } },
        { "futureGroup", QJsonObject{ { "keep", true } } },
        { "routeConfig", QJsonObject{ { "domainStrategy", "AsIs" }, { "futureRoute", true } } },
    };
    Fixture fixture(QJsonDocument(QJsonObject{ { "test-route", original }, { "opaque-root", QJsonArray{ 1, 2 } } }).toJson());
    const GroupRoutingId id{ "test-route" };
    auto [overrideDns, dns, fake] = RouteManager->GetDNSSettings(id);
    if (edit)
        dns.tag = "changed";
    REQUIRE(RouteManager->SetDNSSettings(id, overrideDns, dns, fake));
    REQUIRE(RouteManager->SaveRoutes());
    const auto saved = JsonFromString(StringFromFile(application->ConfigPath + "routes.json"));
    auto expected = original;
    if (edit)
    {
        auto object = expected.value("dnsConfig").toObject();
        object["tag"] = "changed";
        expected["dnsConfig"] = object;
    }
    REQUIRE(saved.value("test-route") == expected);
    const QJsonArray opaque{ 1, 2 };
    REQUIRE(saved.value("opaque-root") == opaque);
}

TEST_CASE("Actual filesystem save failure also keeps group selection and pending DNS edits")
{
    Fixture fixture;
    ConnectionManager->CreateGroup("second", false);
    GroupManager manager;
    manager.show();
    application->processEvents();
    auto *list = Child<QListWidget>(manager, "groupList");
    auto *previous = list->currentItem();
    auto *target = list->item(list->row(previous) == 0 ? 1 : 0);
    EditLine(Child<QLineEdit>(manager, "dnsTagTxt"), "pending-tag");
    REQUIRE(QDir().mkpath(application->ConfigPath + "routes.json"));
    application->modalWarnings = true;
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(target).center());
    application->processEvents();
    REQUIRE(application->warnings == 1);
    REQUIRE(list->currentItem() == previous);
    REQUIRE(Child<QLineEdit>(manager, "dnsTagTxt")->text() == "pending-tag");
    REQUIRE(QDir(application->ConfigPath + "routes.json").removeRecursively());
    list->setCurrentItem(target);
    REQUIRE(list->currentItem() == target);
    REQUIRE(application->warnings == 1);
    const auto routeId = ConnectionManager->GetGroupRoutingId(GroupId{ previous->data(Qt::UserRole).toString() });
    REQUIRE(std::get<1>(RouteManager->GetDNSSettings(routeId)).tag == "pending-tag");
    REQUIRE(QFile::exists(application->ConfigPath + "routes.json"));
}

TEST_CASE("Removing a selected group does not save or reload the deleted identifier")
{
    Fixture fixture;
    const auto second = ConnectionManager->CreateGroup("second", false);
    REQUIRE(second != NullGroupId);
    GroupManager manager;
    auto *list = Child<QListWidget>(manager, "groupList");
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == second.toString())
            list->setCurrentItem(list->item(i));
    REQUIRE(QMetaObject::invokeMethod(&manager, "on_removeGroupButton_clicked", Qt::DirectConnection));
    REQUIRE(application->warnings == 0);
    REQUIRE_FALSE(ConnectionManager->AllGroups().contains(second));
    REQUIRE(list->currentItem());
    REQUIRE(Child<QLineEdit>(manager, "groupNameTxt")->text() == list->currentItem()->text());
}

TEST_CASE("Group export rejects damaged route dependencies before any file dialog or file write")
{
    const auto count = GENERATE(1, 2);
    Fixture fixture("{broken");
    const CONFIGROOT root(QJsonObject{ { "outbounds", QJsonArray{ QJsonObject{ { "protocol", "freedom" }, { "tag", "direct" } } } } });
    for (int i = 0; i < count; ++i)
        REQUIRE_FALSE(ConnectionManager->CreateConnection(root, QStringLiteral("export-%1").arg(i)).isEmpty());
    GroupManager manager;
    auto *table = Child<QTableWidget>(manager, "connectionsTable");
    for (int i = 0; i < table->rowCount(); ++i)
        for (int col = 0; col < table->columnCount(); ++col)
            table->item(i, col)->setSelected(true);
    REQUIRE(QMetaObject::invokeMethod(&manager, "onRCMExportConnectionTriggered", Qt::DirectConnection));
    REQUIRE(application->warnings == 1);
    REQUIRE(application->warning.contains("routes.json"));
    REQUIRE(StringFromFile(application->ConfigPath + "routes.json") == "{broken");
}

TEST_CASE("Declining group deletion and canceling the dialog do not persist pending route edits")
{
    Fixture fixture("{}");
    const auto second = ConnectionManager->CreateGroup("second", false);
    REQUIRE(second != NullGroupId);
    GroupManager manager;
    auto *list = Child<QListWidget>(manager, "groupList");
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == second.toString())
            list->setCurrentItem(list->item(i));
    const auto before = StringFromFile(application->ConfigPath + "routes.json");
    EditLine(Child<QLineEdit>(manager, "dnsTagTxt"), "uncommitted-tag");
    application->answer = No;
    REQUIRE(QMetaObject::invokeMethod(&manager, "on_removeGroupButton_clicked", Qt::DirectConnection));
    REQUIRE(application->warnings == 0);
    REQUIRE(ConnectionManager->AllGroups().contains(second));
    REQUIRE(StringFromFile(application->ConfigPath + "routes.json") == before);
    manager.reject();
    REQUIRE(StringFromFile(application->ConfigPath + "routes.json") == before);
    const auto routeId = ConnectionManager->GetGroupRoutingId(second);
    REQUIRE(std::get<1>(RouteManager->GetDNSSettings(routeId)).tag != "uncommitted-tag");
}
