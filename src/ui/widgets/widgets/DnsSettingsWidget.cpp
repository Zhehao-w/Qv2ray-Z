#include "DnsSettingsWidget.hpp"

#include "components/geosite/QvGeositeReader.hpp"
#include "core/connection/Generation.hpp"
#include "ui/widgets/common/WidgetUIBase.hpp"
#include "ui/widgets/widgets/QvAutoCompleteTextEdit.hpp"
#include "utils/QvHelpers.hpp"

#include <QScopedValueRollback>

using Qv2ray::common::validation::IsIPv4Address;
using Qv2ray::common::validation::IsIPv6Address;
using Qv2ray::common::validation::IsValidDNSServer;
using Qv2ray::common::validation::IsValidIPAddress;

#define CHECK_DISABLE_MOVE_BTN                                                                                                                       \
    if (serversListbox->count() <= 1)                                                                                                                \
    {                                                                                                                                                \
        moveServerUpBtn->setEnabled(false);                                                                                                          \
        moveServerDownBtn->setEnabled(false);                                                                                                        \
    }

#define UPDATE_UI_ENABLED_STATE                                                                                                                      \
    detailsSettingsGB->setEnabled(serversListbox->count() > 0);                                                                                      \
    serverAddressTxt->setEnabled(serversListbox->count() > 0);                                                                                       \
    removeServerBtn->setEnabled(serversListbox->count() > 0);                                                                                        \
    ProcessDnsPortEnabledState();                                                                                                                    \
    CHECK_DISABLE_MOVE_BTN

#define currentServerIndex serversListbox->currentRow()

void DnsSettingsWidget::updateColorScheme()
{
    addServerBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("add")));
    removeServerBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("minus")));
    moveServerUpBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("arrow-up")));
    moveServerDownBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("arrow-down")));
    addStaticHostBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("add")));
    removeStaticHostBtn->setIcon(QIcon(QV2RAY_COLORSCHEME_FILE("minus")));
}

DnsSettingsWidget::DnsSettingsWidget(QWidget *parent) : QWidget(parent)
{
    setupUi(this);
    QvMessageBusConnect(DnsSettingsWidget);
    //
    auto sourceStringsDomain = ReadGeoSiteFromFile(GlobalConfig.kernelConfig.AssetsPath() + "/geosite.dat");
    auto sourceStringsIP = ReadGeoSiteFromFile(GlobalConfig.kernelConfig.AssetsPath() + "/geoip.dat");
    //
    domainListTxt = new AutoCompleteTextEdit("geosite", sourceStringsDomain, this);
    ipListTxt = new AutoCompleteTextEdit("geoip", sourceStringsIP, this);
    connect(domainListTxt, &AutoCompleteTextEdit::textChanged,
            [&]()
            {
                if (!isLoading && currentServerIndex >= 0)
                    this->dns.servers[currentServerIndex].domains = SplitLines(domainListTxt->toPlainText());
            });
    connect(ipListTxt, &AutoCompleteTextEdit::textChanged,
            [&]()
            {
                if (!isLoading && currentServerIndex >= 0)
                    this->dns.servers[currentServerIndex].expectIPs = SplitLines(ipListTxt->toPlainText());
            });

    domainsLayout->addWidget(domainListTxt);
    expectedIPsLayout->addWidget(ipListTxt);
    detailsSettingsGB->setCheckable(true);
    detailsSettingsGB->setChecked(false);
    UPDATE_UI_ENABLED_STATE;
    updateColorScheme();
}

QvMessageBusSlotImpl(DnsSettingsWidget)
{
    switch (msg)
    {
        MBRetranslateDefaultImpl;
        case HIDE_WINDOWS:
        case SHOW_WINDOWS: break;
        case UPDATE_COLORSCHEME:
        {
            updateColorScheme();
            break;
        }
    }
}

void DnsSettingsWidget::SetDNSObject(const DNSObject &_dns, const FakeDNSObject &_fakeDNS)
{
    const QScopedValueRollback<bool> loading(isLoading, true);
    preserveJson = false;
    serverJsonStates.clear();
    this->dns = _dns;
    this->fakeDNS = _fakeDNS;

    dnsClientIPTxt->setText(dns.clientIp);
    dnsTagTxt->setText(dns.tag);

    serversListbox->clear();
    std::for_each(dns.servers.begin(), dns.servers.end(), [&](const auto &dns) { serversListbox->addItem(dns.address); });

    if (serversListbox->count() > 0)
    {
        serversListbox->setCurrentRow(0);
        ShowCurrentDnsServerDetails();
    }

    staticResolvedDomainsTable->setRowCount(0);
    for (const auto &[host, ip] : dns.hosts.toStdMap())
    {
        const auto rowId = staticResolvedDomainsTable->rowCount();
        staticResolvedDomainsTable->insertRow(rowId);
        staticResolvedDomainsTable->setItem(rowId, 0, new QTableWidgetItem(host));
        staticResolvedDomainsTable->setItem(rowId, 1, new QTableWidgetItem(ip));
    }
    staticResolvedDomainsTable->resizeColumnsToContents();

    dnsQueryStrategyCB->setCurrentText(dns.queryStrategy);
    dnsDisableFallbackCB->setChecked(dns.disableFallback);
    dnsDisableCacheCB->setChecked(dns.disableCache);

    fakeDNSIPPool->setCurrentText(fakeDNS.ipPool);
    fakeDNSIPPoolSize->setValue(fakeDNS.poolSize);
    UPDATE_UI_ENABLED_STATE
}

bool DnsSettingsWidget::CheckIsValidDNS() const
{
    if (!dns.clientIp.isEmpty() && !IsValidIPAddress(dns.clientIp))
        return false;
    for (const auto &server : dns.servers)
    {
        if (!IsValidDNSServer(server.address))
            return false;
    }
    return true;
}

void DnsSettingsWidget::ProcessDnsPortEnabledState()
{
    if (detailsSettingsGB->isChecked())
    {
        const auto isDoHDoT = serverAddressTxt->text().startsWith("https:") || serverAddressTxt->text().startsWith("https+");
        serverPortSB->setEnabled(!isDoHDoT);
    }
}

void DnsSettingsWidget::ShowCurrentDnsServerDetails()
{
    if (currentServerIndex < 0 || currentServerIndex >= dns.servers.size())
        return;
    const QScopedValueRollback<bool> loading(isLoading, true);
    detailsSettingsGB->setCheckable(!preserveJson || !serverJsonStates[currentServerIndex].original.isObject());
    serverAddressTxt->setText(dns.servers[currentServerIndex].address);
    //
    domainListTxt->setPlainText(dns.servers[currentServerIndex].domains.join(NEWLINE));
    ipListTxt->setPlainText(dns.servers[currentServerIndex].expectIPs.join(NEWLINE));
    //
    serverPortSB->setValue(dns.servers[currentServerIndex].port);
    detailsSettingsGB->setChecked(dns.servers[currentServerIndex].QV2RAY_DNS_IS_COMPLEX_DNS);
    //
    if (serverAddressTxt->text().isEmpty() || IsValidDNSServer(serverAddressTxt->text()))
    {
        BLACK(serverAddressTxt);
    }
    else
    {
        RED(serverAddressTxt);
    }
    ProcessDnsPortEnabledState();
}

std::pair<DNSObject, FakeDNSObject> DnsSettingsWidget::GetDNSObject()
{
    dns.hosts.clear();
    for (auto i = 0; i < staticResolvedDomainsTable->rowCount(); i++)
    {
        const auto &item1 = staticResolvedDomainsTable->item(i, 0);
        const auto &item2 = staticResolvedDomainsTable->item(i, 1);
        if (item1 && item2)
            dns.hosts[item1->text()] = item2->text();
    }
    return { dns, fakeDNS };
}

void DnsSettingsWidget::on_dnsClientIPTxt_textEdited(const QString &arg1)
{
    if (isLoading)
        return;
    dns.clientIp = arg1;
}

void DnsSettingsWidget::on_dnsTagTxt_textEdited(const QString &arg1)
{
    if (isLoading)
        return;
    dns.tag = arg1;
}
void DnsSettingsWidget::on_addServerBtn_clicked()
{
    DNSObject::DNSServerObject o;
    o.address = "1.1.1.1";
    o.port = 53;
    dns.servers.push_back(o);
    if (preserveJson)
        serverJsonStates.push_back({ QJsonValue(QJsonValue::Undefined), o });
    serversListbox->addItem(o.address);
    serversListbox->setCurrentRow(serversListbox->count() - 1);
    UPDATE_UI_ENABLED_STATE
    ShowCurrentDnsServerDetails();
}

void DnsSettingsWidget::on_removeServerBtn_clicked()
{
    if (currentServerIndex < 0)
        return;
    if (preserveJson)
        serverJsonStates.removeAt(currentServerIndex);
    dns.servers.removeAt(currentServerIndex);
    // Block the signals
    serversListbox->blockSignals(true);
    auto item = serversListbox->item(currentServerIndex);
    serversListbox->removeItemWidget(item);
    delete item;
    serversListbox->blockSignals(false);
    UPDATE_UI_ENABLED_STATE

    if (serversListbox->count() > 0)
    {
        if (currentServerIndex < 0)
            serversListbox->setCurrentRow(0);
        ShowCurrentDnsServerDetails();
    }
}

void DnsSettingsWidget::on_serversListbox_currentRowChanged(int currentRow)
{
    if (currentRow < 0)
        return;

    moveServerUpBtn->setEnabled(true);
    moveServerDownBtn->setEnabled(true);
    if (currentRow == 0)
    {
        moveServerUpBtn->setEnabled(false);
    }
    if (currentRow == serversListbox->count() - 1)
    {
        moveServerDownBtn->setEnabled(false);
    }

    ShowCurrentDnsServerDetails();
}

void DnsSettingsWidget::on_moveServerUpBtn_clicked()
{
    if (preserveJson)
        serverJsonStates.swapItemsAt(currentServerIndex - 1, currentServerIndex);
    auto temp = dns.servers[currentServerIndex - 1];
    dns.servers[currentServerIndex - 1] = dns.servers[currentServerIndex];
    dns.servers[currentServerIndex] = temp;

    serversListbox->currentItem()->setText(dns.servers[currentServerIndex].address);
    serversListbox->setCurrentRow(currentServerIndex - 1);
    serversListbox->currentItem()->setText(dns.servers[currentServerIndex].address);
}

void DnsSettingsWidget::on_moveServerDownBtn_clicked()
{
    if (preserveJson)
        serverJsonStates.swapItemsAt(currentServerIndex + 1, currentServerIndex);
    auto temp = dns.servers[currentServerIndex + 1];
    dns.servers[currentServerIndex + 1] = dns.servers[currentServerIndex];
    dns.servers[currentServerIndex] = temp;

    serversListbox->currentItem()->setText(dns.servers[currentServerIndex].address);
    serversListbox->setCurrentRow(currentServerIndex + 1);
    serversListbox->currentItem()->setText(dns.servers[currentServerIndex].address);
}

void DnsSettingsWidget::on_serverAddressTxt_textEdited(const QString &arg1)
{
    if (currentServerIndex < 0)
        return;
    if (isLoading)
        return;
    dns.servers[currentServerIndex].address = arg1;
    serversListbox->currentItem()->setText(arg1);
    if (arg1.isEmpty() || IsValidDNSServer(arg1))
    {
        BLACK(serverAddressTxt);
    }
    else
    {
        RED(serverAddressTxt);
    }

    ProcessDnsPortEnabledState();
}

void DnsSettingsWidget::on_serverPortSB_valueChanged(int arg1)
{
    if (currentServerIndex < 0)
        return;
    if (isLoading)
        return;
    dns.servers[currentServerIndex].port = arg1;
}

void DnsSettingsWidget::on_addStaticHostBtn_clicked()
{
    if (staticResolvedDomainsTable->rowCount() >= 0)
        staticResolvedDomainsTable->insertRow(staticResolvedDomainsTable->rowCount());
}

void DnsSettingsWidget::on_removeStaticHostBtn_clicked()
{
    if (staticResolvedDomainsTable->rowCount() >= 0)
        staticResolvedDomainsTable->removeRow(staticResolvedDomainsTable->currentRow());
    staticResolvedDomainsTable->resizeColumnsToContents();
}

void DnsSettingsWidget::on_staticResolvedDomainsTable_cellChanged(int, int)
{
    staticResolvedDomainsTable->resizeColumnsToContents();
}

void DnsSettingsWidget::on_detailsSettingsGB_toggled(bool arg1)
{
    if (isLoading)
        return;
    if (currentServerIndex >= 0)
        dns.servers[currentServerIndex].QV2RAY_DNS_IS_COMPLEX_DNS = arg1;
    // detailsSettingsGB->setChecked(dns.servers[currentServerIndex].QV2RAY_DNS_IS_COMPLEX_DNS);
}

void DnsSettingsWidget::on_fakeDNSIPPool_currentTextChanged(const QString &arg1)
{
    if (isLoading)
        return;
    fakeDNS.ipPool = arg1;
}

void DnsSettingsWidget::on_fakeDNSIPPoolSize_valueChanged(int arg1)
{
    if (isLoading)
        return;
    fakeDNS.poolSize = arg1;
}

void DnsSettingsWidget::on_dnsDisableCacheCB_stateChanged(int arg1)
{
    if (isLoading)
        return;
    dns.disableCache = arg1 == Qt::Checked;
}

void DnsSettingsWidget::on_dnsDisableFallbackCB_stateChanged(int arg1)
{
    if (isLoading)
        return;
    dns.disableFallback = arg1 == Qt::Checked;
}

void DnsSettingsWidget::on_dnsQueryStrategyCB_currentTextChanged(const QString &arg1)
{
    if (isLoading)
        return;
    dns.queryStrategy = arg1;
}

void DnsSettingsWidget::SetDNSJson(const QJsonValue &value)
{
    auto model = DNSObject::fromJson(value.toObject());
    const auto servers = value.toObject().value("servers").toArray();
    for (int i = 0; i < servers.size(); ++i)
        model.servers[i].QV2RAY_DNS_IS_COMPLEX_DNS = servers[i].isObject();
    SetDNSObject(model, fakeDNS);
    const QScopedValueRollback<bool> loading(isLoading, true);
    originalJson = value;
    baselineDns = dns;
    preserveJson = true;
    for (int i = 0; i < servers.size(); ++i)
        serverJsonStates.push_back({ servers[i], dns.servers[i] });

    const auto hosts = value.toObject().value("hosts").toObject();
    for (int row = 0; row < staticResolvedDomainsTable->rowCount(); ++row)
    {
        const auto key = staticResolvedDomainsTable->item(row, 0)->text();
        auto item = staticResolvedDomainsTable->item(row, 1);
        const auto raw = hosts.value(key);
        if (!raw.isString())
        {
            const auto bytes = QJsonDocument(QJsonArray{ raw }).toJson(QJsonDocument::Compact);
            item->setText(QString::fromUtf8(bytes.mid(1, bytes.size() - 2)));
            item->setData(Qt::UserRole, QVariant::fromValue(raw));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(tr("This DNS host value is preserved. Use the JSON editor to change it."));
        }
    }
    ShowCurrentDnsServerDetails();
}

QJsonValue DnsSettingsWidget::GetDNSJson()
{
    GetDNSObject();
    if (!preserveJson)
        return GenerateDNS(dns);
    auto result = originalJson.toObject();
    const auto baseline = baselineDns.toJson();
    const auto current = dns.toJson();
    for (const auto &key : { "clientIp", "tag", "disableCache", "disableFallback", "queryStrategy" })
        if (baseline.value(key) != current.value(key))
            result[key] = current.value(key);

    QJsonObject hosts;
    for (int row = 0; row < staticResolvedDomainsTable->rowCount(); ++row)
    {
        const auto key = staticResolvedDomainsTable->item(row, 0);
        const auto value = staticResolvedDomainsTable->item(row, 1);
        if (key && value)
            hosts[key->text()] = value->data(Qt::UserRole).isValid() ? value->data(Qt::UserRole).value<QJsonValue>() : QJsonValue(value->text());
    }
    if (hosts != originalJson.toObject().value("hosts").toObject())
        result["hosts"] = hosts;

    QJsonArray servers;
    for (int i = 0; i < dns.servers.size(); ++i)
    {
        const auto &state = serverJsonStates[i];
        const auto &server = dns.servers[i];
        if (state.original.isUndefined())
        {
            auto object = server.toJson();
            object.remove("QV2RAY_DNS_IS_COMPLEX_DNS");
            servers.append(server.QV2RAY_DNS_IS_COMPLEX_DNS ? QJsonValue(object) : QJsonValue(server.address));
        }
        else if (server == state.baseline)
        {
            servers.append(state.original);
        }
        else if (state.original.isString() && !server.QV2RAY_DNS_IS_COMPLEX_DNS)
        {
            servers.append(server.address);
        }
        else
        {
            auto object = state.original.toObject();
            const auto before = state.baseline.toJson();
            const auto after = server.toJson();
            if (state.original.isString())
                object["address"] = server.address;
            for (const auto &key : { "address", "port", "domains", "expectIPs" })
                if (before.value(key) != after.value(key))
                    object[key] = after.value(key);
            servers.append(object);
        }
    }
    if (servers != originalJson.toObject().value("servers").toArray())
        result["servers"] = servers;
    return result == originalJson.toObject() ? originalJson : QJsonValue(result);
}
