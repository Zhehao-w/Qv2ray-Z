#include "StreamSettingsWidget.hpp"

#include "ui/widgets/common/WidgetUIBase.hpp"
#include "ui/widgets/editors/w_ChainSha256Editor.hpp"
#include "ui/widgets/editors/w_JsonEditor.hpp"
#include "utils/QvHelpers.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#define QV_MODULE_NAME "StreamSettingsWidget"

StreamSettingsWidget::StreamSettingsWidget(QWidget *parent) : QWidget(parent)
{
    setupUi(this);

    // XHTTP is maintained programmatically so the transport/page relationship
    // is explicit instead of depending on matching Designer indices.
    xhttpStackPage = new QWidget(v2rayStackView);
    xhttpStackPage->setObjectName(QStringLiteral("xhttpStackPage"));
    auto *xhttpLayout = new QFormLayout(xhttpStackPage);
    xhttpHostTxt = new QLineEdit(xhttpStackPage);
    xhttpHostTxt->setObjectName(QStringLiteral("xhttpHostTxt"));
    xhttpHostTxt->setPlaceholderText(QStringLiteral("edge.example.com"));
    xhttpPathTxt = new QLineEdit(xhttpStackPage);
    xhttpPathTxt->setObjectName(QStringLiteral("xhttpPathTxt"));
    xhttpPathTxt->setPlaceholderText(QStringLiteral("/"));
    xhttpModeCB = new QComboBox(xhttpStackPage);
    xhttpModeCB->setObjectName(QStringLiteral("xhttpModeCB"));
    xhttpExtraTxt = new QPlainTextEdit(xhttpStackPage);
    xhttpExtraTxt->setObjectName(QStringLiteral("xhttpExtraTxt"));
    xhttpExtraTxt->setReadOnly(true);
    xhttpExtraTxt->setLineWrapMode(QPlainTextEdit::NoWrap);
    xhttpExtraTxt->setMinimumHeight(96);
    xhttpExtraTxt->setTabChangesFocus(true);
    xhttpModeCB->setEditable(true);
    xhttpModeCB->setInsertPolicy(QComboBox::NoInsert);
    xhttpModeCB->addItems({ "auto", "packet-up", "stream-up", "stream-one" });
    xhttpModeCB->setToolTip(tr("Select a common XHTTP mode or enter a mode supported by the bundled Xray."));

    auto *extraContainer = new QWidget(xhttpStackPage);
    extraContainer->setObjectName(QStringLiteral("xhttpExtraContainer"));
    auto *extraLayout = new QVBoxLayout(extraContainer);
    extraLayout->setContentsMargins(0, 0, 0, 0);
    extraLayout->addWidget(xhttpExtraTxt);
    auto *extraButtons = new QHBoxLayout;
    auto *editExtraBtn = new QPushButton(tr("Edit"), extraContainer);
    editExtraBtn->setObjectName(QStringLiteral("xhttpEditExtraBtn"));
    auto *resetExtraBtn = new QPushButton(tr("Reset"), extraContainer);
    resetExtraBtn->setObjectName(QStringLiteral("xhttpResetExtraBtn"));
    extraButtons->addWidget(editExtraBtn);
    extraButtons->addWidget(resetExtraBtn);
    extraButtons->addStretch();
    extraLayout->addLayout(extraButtons);

    xhttpLayout->addRow(tr("Host"), xhttpHostTxt);
    xhttpLayout->addRow(tr("Path"), xhttpPathTxt);
    xhttpLayout->addRow(tr("Mode"), xhttpModeCB);
    xhttpLayout->addRow(tr("Extra"), extraContainer);
    xhttpExtraTxt->setToolTip(tr("Passed to Xray as xhttpSettings.extra without interpreting the XHTTP grammar."));

    v2rayStackView->addWidget(xhttpStackPage);
    transportCombo->addItem(QStringLiteral("xhttp"));

    QWidget::setTabOrder(transportCombo, xhttpHostTxt);
    QWidget::setTabOrder(xhttpHostTxt, xhttpPathTxt);
    QWidget::setTabOrder(xhttpPathTxt, xhttpModeCB);
    QWidget::setTabOrder(xhttpModeCB, xhttpExtraTxt);
    QWidget::setTabOrder(xhttpExtraTxt, editExtraBtn);
    QWidget::setTabOrder(editExtraBtn, resetExtraBtn);

    connect(xhttpHostTxt, &QLineEdit::textEdited, this, [this](const QString &value) { stream.xhttpSettings["host"] = value; });
    connect(xhttpPathTxt, &QLineEdit::textEdited, this, [this](const QString &value) { stream.xhttpSettings["path"] = value; });
    connect(xhttpModeCB, &QComboBox::currentTextChanged, this, [this](const QString &value) { stream.xhttpSettings["mode"] = value; });
    connect(editExtraBtn, &QPushButton::clicked, this, [this]() {
        const auto current = stream.xhttpSettings.value("extra");
        JsonEditor editor(current.isObject() ? current.toObject() : QJsonObject{}, this);
        stream.xhttpSettings["extra"] = editor.OpenEditor();
        RefreshXhttpExtraText();
    });
    connect(resetExtraBtn, &QPushButton::clicked, this, [this]() {
        stream.xhttpSettings.remove("extra");
        RefreshXhttpExtraText();
    });

    QvMessageBusConnect(StreamSettingsWidget);
}

QvMessageBusSlotImpl(StreamSettingsWidget)
{
    switch (msg)
    {
        MBRetranslateDefaultImpl;
        case UPDATE_COLORSCHEME:
        case HIDE_WINDOWS:
        case SHOW_WINDOWS: break;
    }
}

StreamSettingsObject StreamSettingsWidget::GetStreamSettings() const
{
    return stream;
}

bool StreamSettingsWidget::SelectTransportEditor(const QString &network)
{
    QWidget *page = nullptr;
    switch (StreamTransportEditorForNetwork(network))
    {
        case StreamTransportEditor::Tcp: page = tcpStackPage; break;
        case StreamTransportEditor::Http: page = httpStackPage; break;
        case StreamTransportEditor::WebSocket: page = wsStackPage; break;
        case StreamTransportEditor::Kcp: page = mKCPStackPage; break;
        case StreamTransportEditor::DomainSocket: page = dsStackPage; break;
        case StreamTransportEditor::Quic: page = quicStackPage; break;
        case StreamTransportEditor::Grpc: page = grpcStackPage; break;
        case StreamTransportEditor::Xhttp: page = xhttpStackPage; break;
        case StreamTransportEditor::Invalid: break;
    }
    if (!page)
    {
        v2rayStackView->setCurrentIndex(-1);
        return false;
    }
    v2rayStackView->setCurrentWidget(page);
    return true;
}

void StreamSettingsWidget::RefreshXhttpExtraText()
{
    const auto extra = stream.xhttpSettings.value("extra");
    if (extra.isUndefined() || extra.isNull())
    {
        xhttpExtraTxt->setPlainText("{}");
        BLACK(xhttpExtraTxt);
    }
    else if (extra.isObject())
    {
        xhttpExtraTxt->setPlainText(JsonToString(extra.toObject()));
        BLACK(xhttpExtraTxt);
    }
    else
    {
        xhttpExtraTxt->setPlainText(tr("Invalid XHTTP extra: expected a JSON object"));
        RED(xhttpExtraTxt);
    }
}

void StreamSettingsWidget::SetStreamObject(const StreamSettingsObject &sso)
{
    stream = sso;
    {
        const QSignalBlocker blocker(transportCombo);
        transportCombo->setCurrentIndex(transportCombo->findText(stream.network, Qt::MatchExactly));
    }
    if (!SelectTransportEditor(stream.network))
        LOG("Unsupported Transport Type:", stream.network);
    // TLS and REALITY
    {
        const static QMap<QString, int> securityIndexMap{ { "none", 0 }, { "tls", 1 }, { "reality", 2 } };
        if (securityIndexMap.contains(stream.security))
            securityTypeCB->setCurrentIndex(securityIndexMap[stream.security]);
        else
            LOG("Unsupported Security Type:", stream.security);

        serverNameTxt->setText(stream.tlsSettings.serverName);
        enableSessionResumptionCB->setChecked(stream.tlsSettings.enableSessionResumption);
        disableSystemRoot->setChecked(stream.tlsSettings.disableSystemRoot);
        alpnTxt->setText(stream.tlsSettings.alpn.join("|"));

        if (stream.security == "reality")
            serverNameTxt->setText(stream.realitySettings.serverName);
        fingerprintTxt->setText(stream.security == "reality" ? stream.realitySettings.fingerprint : stream.tlsSettings.fingerprint);
        realityPasswordTxt->setText(stream.realitySettings.password);
        realityShortIdTxt->setText(stream.realitySettings.shortId);
        realityMldsa65VerifyTxt->setText(stream.realitySettings.mldsa65Verify);
        realitySpiderXTxt->setText(stream.realitySettings.spiderX);
        on_securityTypeCB_currentIndexChanged(securityTypeCB->currentIndex());
    }
    // TCP
    {
        tcpHeaderTypeCB->setCurrentText(stream.tcpSettings.header.type);
        tcpRequestTxt->setPlainText(JsonToString(stream.tcpSettings.header.request.toJson()));
        tcpRespTxt->setPlainText(JsonToString(stream.tcpSettings.header.response.toJson()));
    }
    // HTTP
    {
        httpHostTxt->setPlainText(stream.httpSettings.host.join(NEWLINE));
        httpPathTxt->setText(stream.httpSettings.path);
        httpMethodCB->setCurrentText(stream.httpSettings.method);
        httpHeadersTxt->setPlainText(JsonToString(stream.httpSettings.toJson()["headers"].toObject()));
    }
    // WS
    {
        wsPathTxt->setText(stream.wsSettings.path);
        QString wsHeaders;
        for (const auto &[key, value] : stream.wsSettings.headers.toStdMap())
        {
            wsHeaders = wsHeaders % key % "|" % value % NEWLINE;
        }
        wsHeadersTxt->setPlainText(wsHeaders);
        wsEarlyDataSB->setValue(stream.wsSettings.maxEarlyData);
        wsBrowserForwardCB->setChecked(stream.wsSettings.useBrowserForwarding);
        wsBrowserForwardCB->hide();
        label_25->hide();
        wsEarlyDataHeaderNameCB->setCurrentText(stream.wsSettings.earlyDataHeaderName);
    }
    // mKCP
    {
        kcpMTU->setValue(stream.kcpSettings.mtu);
        kcpTTI->setValue(stream.kcpSettings.tti);
        kcpHeaderType->setCurrentText(stream.kcpSettings.header.type);
        kcpCongestionCB->setChecked(stream.kcpSettings.congestion);
        kcpReadBufferSB->setValue(stream.kcpSettings.readBufferSize);
        kcpUploadCapacSB->setValue(stream.kcpSettings.uplinkCapacity);
        kcpDownCapacitySB->setValue(stream.kcpSettings.downlinkCapacity);
        kcpWriteBufferSB->setValue(stream.kcpSettings.writeBufferSize);
        kcpSeedTxt->setText(stream.kcpSettings.seed);
    }
    // DS
    {
        dsPathTxt->setText(stream.dsSettings.path);
    }
    // QUIC
    {
        quicKeyTxt->setText(stream.quicSettings.key);
        quicSecurityCB->setCurrentText(stream.quicSettings.security);
        quicHeaderTypeCB->setCurrentText(stream.quicSettings.header.type);
    }
    // gRPC
    {
        grpcServiceNameTxt->setText(stream.grpcSettings.serviceName);
        grpcModeCB->setCurrentText(stream.grpcSettings.multiMode ? "multi" : "gun");
    }
    // XHTTP
    {
        const QSignalBlocker hostBlocker(xhttpHostTxt);
        const QSignalBlocker pathBlocker(xhttpPathTxt);
        const QSignalBlocker modeBlocker(xhttpModeCB);
        xhttpHostTxt->setText(stream.xhttpSettings.value("host").toString());
        xhttpPathTxt->setText(stream.xhttpSettings.value("path").toString("/"));
        xhttpModeCB->setCurrentText(stream.xhttpSettings.value("mode").toString("auto"));
        RefreshXhttpExtraText();
    }
    // SOCKOPT
    {
        tProxyCB->setCurrentText(stream.sockopt.tproxy);
        tcpFastOpenCB->setChecked(stream.sockopt.tcpFastOpen);
        soMarkSpinBox->setValue(stream.sockopt.mark);
        tcpKeepAliveIntervalSpinBox->setValue(stream.sockopt.tcpKeepAliveInterval);
    }
}

void StreamSettingsWidget::on_httpPathTxt_textEdited(const QString &arg1)
{
    stream.httpSettings.path = arg1;
}

void StreamSettingsWidget::on_httpHostTxt_textChanged()
{
    const auto hosts = httpHostTxt->toPlainText().replace("\r", "").split("\n");
    stream.httpSettings.host.clear();
    for (const auto &host : hosts)
    {
        if (!host.trimmed().isEmpty())
            stream.httpSettings.host.push_back(host.trimmed());
    }
}

void StreamSettingsWidget::on_wsHeadersTxt_textChanged()
{
    const auto headers = SplitLines(wsHeadersTxt->toPlainText());
    stream.wsSettings.headers.clear();
    for (const auto &header : headers)
    {
        if (header.isEmpty())
            continue;

        if (!header.contains("|"))
        {
            LOG("Header missing '|' separator");
            RED(wsHeadersTxt);
            return;
        }

        const auto index = header.indexOf("|");
        auto key = header.left(index);
        auto value = header.right(header.length() - index - 1);
        stream.wsSettings.headers[key] = value;
    }
    BLACK(wsHeadersTxt);
}

void StreamSettingsWidget::on_tcpRequestDefBtn_clicked()
{
    tcpRequestTxt->clear();
    tcpRequestTxt->setPlainText(JsonToString(HTTPRequestObject().toJson()));
    stream.tcpSettings.header.request = HTTPRequestObject();
}

void StreamSettingsWidget::on_tcpRespDefBtn_clicked()
{
    tcpRespTxt->clear();
    tcpRespTxt->setPlainText(JsonToString(HTTPResponseObject().toJson()));
    stream.tcpSettings.header.response = HTTPResponseObject();
}

void StreamSettingsWidget::on_soMarkSpinBox_valueChanged(int arg1)
{
    stream.sockopt.mark = arg1;
}

void StreamSettingsWidget::on_tcpFastOpenCB_stateChanged(int arg1)
{
    stream.sockopt.tcpFastOpen = arg1 == Qt::Checked;
}

void StreamSettingsWidget::on_tProxyCB_currentIndexChanged(int arg1)
{
    stream.sockopt.tproxy = tProxyCB->itemText(arg1);
}

void StreamSettingsWidget::on_quicSecurityCB_currentIndexChanged(int arg1)
{
    stream.quicSettings.security = quicSecurityCB->itemText(arg1);
}

void StreamSettingsWidget::on_quicKeyTxt_textEdited(const QString &arg1)
{
    stream.quicSettings.key = arg1;
}

void StreamSettingsWidget::on_quicHeaderTypeCB_currentIndexChanged(int arg1)
{
    stream.quicSettings.header.type = quicHeaderTypeCB->itemText(arg1);
}

void StreamSettingsWidget::on_tcpHeaderTypeCB_currentIndexChanged(int arg1)
{
    stream.tcpSettings.header.type = tcpHeaderTypeCB->itemText(arg1);
}

void StreamSettingsWidget::on_wsPathTxt_textEdited(const QString &arg1)
{
    stream.wsSettings.path = arg1;
}

void StreamSettingsWidget::on_kcpMTU_valueChanged(int arg1)
{
    stream.kcpSettings.mtu = arg1;
}

void StreamSettingsWidget::on_kcpTTI_valueChanged(int arg1)
{
    stream.kcpSettings.tti = arg1;
}

void StreamSettingsWidget::on_kcpUploadCapacSB_valueChanged(int arg1)
{
    stream.kcpSettings.uplinkCapacity = arg1;
}

void StreamSettingsWidget::on_kcpCongestionCB_stateChanged(int arg1)
{
    stream.kcpSettings.congestion = arg1 == Qt::Checked;
}

void StreamSettingsWidget::on_kcpDownCapacitySB_valueChanged(int arg1)
{
    stream.kcpSettings.downlinkCapacity = arg1;
}

void StreamSettingsWidget::on_kcpReadBufferSB_valueChanged(int arg1)
{
    stream.kcpSettings.readBufferSize = arg1;
}

void StreamSettingsWidget::on_kcpWriteBufferSB_valueChanged(int arg1)
{
    stream.kcpSettings.writeBufferSize = arg1;
}

void StreamSettingsWidget::on_kcpHeaderType_currentIndexChanged(int arg1)
{
    stream.kcpSettings.header.type = kcpHeaderType->itemText(arg1);
}

void StreamSettingsWidget::on_kcpSeedTxt_textEdited(const QString &arg1)
{
    stream.kcpSettings.seed = arg1;
}

void StreamSettingsWidget::on_dsPathTxt_textEdited(const QString &arg1)
{
    stream.dsSettings.path = arg1;
}

void StreamSettingsWidget::on_tcpRequestEditBtn_clicked()
{
    JsonEditor w(JsonFromString(tcpRequestTxt->toPlainText()), this);
    auto rJson = w.OpenEditor();
    tcpRequestTxt->setPlainText(JsonToString(rJson));
    auto tcpReqObject = HTTPRequestObject::fromJson(rJson);
    stream.tcpSettings.header.request = tcpReqObject;
}

void StreamSettingsWidget::on_tcpResponseEditBtn_clicked()
{
    JsonEditor w(JsonFromString(tcpRespTxt->toPlainText()), this);
    auto rJson = w.OpenEditor();
    tcpRespTxt->setPlainText(JsonToString(rJson));
    auto tcpRspObject = HTTPResponseObject::fromJson(rJson);
    stream.tcpSettings.header.response = tcpRspObject;
}

void StreamSettingsWidget::on_transportCombo_currentIndexChanged(int arg1)
{
    if (arg1 < 0)
        return;
    const auto network = transportCombo->itemText(arg1);
    if (!SelectTransportEditor(network))
    {
        LOG("Unsupported Transport Type:", network);
        return;
    }
    stream.network = network;
    if (network == "xhttp")
    {
        if (!stream.xhttpSettings.contains("path"))
            stream.xhttpSettings["path"] = "/";
        if (!stream.xhttpSettings.contains("mode"))
            stream.xhttpSettings["mode"] = "auto";
    }
}

void StreamSettingsWidget::on_securityTypeCB_currentIndexChanged(int arg1)
{
    stream.security = securityTypeCB->itemText(arg1).toLower();
    const auto isReality = stream.security == "reality";
    realityPasswordLabel->setVisible(isReality);
    realityPasswordTxt->setVisible(isReality);
    realityShortIdLabel->setVisible(isReality);
    realityShortIdTxt->setVisible(isReality);
    realityMldsa65VerifyLabel->setVisible(isReality);
    realityMldsa65VerifyTxt->setVisible(isReality);
    realitySpiderXLabel->setVisible(isReality);
    realitySpiderXTxt->setVisible(isReality);
    enableSessionResumptionCB->setVisible(!isReality);
    disableSystemRoot->setVisible(!isReality);
    alpnLabel->setVisible(!isReality);
    alpnTxt->setVisible(!isReality);
    certificatesLabel->setVisible(!isReality);
    openCertEditorBtn->setVisible(!isReality);
    pinnedPeerCertificateChainSha256Btn->setVisible(!isReality);
}

void StreamSettingsWidget::on_serverNameTxt_textEdited(const QString &arg1)
{
    stream.tlsSettings.serverName = arg1.trimmed();
    stream.realitySettings.serverName = arg1.trimmed();
}

void StreamSettingsWidget::on_fingerprintTxt_textEdited(const QString &arg1)
{
    stream.tlsSettings.fingerprint = arg1.trimmed();
    stream.realitySettings.fingerprint = arg1.trimmed();
}

void StreamSettingsWidget::on_realityPasswordTxt_textEdited(const QString &arg1)
{
    stream.realitySettings.password = arg1.trimmed();
}

void StreamSettingsWidget::on_realityShortIdTxt_textEdited(const QString &arg1)
{
    stream.realitySettings.shortId = arg1.trimmed();
}

void StreamSettingsWidget::on_realityMldsa65VerifyTxt_textEdited(const QString &arg1)
{
    stream.realitySettings.mldsa65Verify = arg1;
}

void StreamSettingsWidget::on_realitySpiderXTxt_textEdited(const QString &arg1)
{
    stream.realitySettings.spiderX = arg1;
}

void StreamSettingsWidget::on_enableSessionResumptionCB_stateChanged(int arg1)
{
    stream.tlsSettings.enableSessionResumption = arg1 == Qt::Checked;
}

void StreamSettingsWidget::on_alpnTxt_textEdited(const QString &arg1)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    stream.tlsSettings.alpn = arg1.split('|', Qt::SplitBehaviorFlags::SkipEmptyParts);
#else
    stream.tlsSettings.alpn = arg1.split('|', QString::SkipEmptyParts);
#endif
}

void StreamSettingsWidget::on_disableSystemRoot_stateChanged(int arg1)
{
    stream.tlsSettings.disableSystemRoot = arg1;
}

void StreamSettingsWidget::on_openCertEditorBtn_clicked()
{
}

void StreamSettingsWidget::on_grpcServiceNameTxt_textEdited(const QString &arg1)
{
    stream.grpcSettings.serviceName = arg1;
}

void StreamSettingsWidget::on_grpcModeCB_currentIndexChanged(int arg1)
{
    stream.grpcSettings.multiMode = grpcModeCB->itemText(arg1).toLower() == "multi";
}

void StreamSettingsWidget::on_wsEarlyDataSB_valueChanged(int arg1)
{
    stream.wsSettings.maxEarlyData = arg1;
}

void StreamSettingsWidget::on_wsBrowserForwardCB_stateChanged(int arg1)
{
    stream.wsSettings.useBrowserForwarding = arg1 == Qt::Checked;
}

void StreamSettingsWidget::on_pinnedPeerCertificateChainSha256Btn_clicked()
{
    ChainSha256Editor ed(this, stream.tlsSettings.pinnedPeerCertificateChainSha256);
    if (ed.exec() == QDialog::Accepted)
    {
        stream.tlsSettings.pinnedPeerCertificateChainSha256 = QList<QString>(ed);
    }
}

void StreamSettingsWidget::on_wsEarlyDataHeaderNameCB_currentIndexChanged(int arg1)
{
    stream.wsSettings.earlyDataHeaderName = wsEarlyDataHeaderNameCB->itemText(arg1);
}

void StreamSettingsWidget::on_httpMethodCB_currentTextChanged(const QString &arg1)
{
    stream.httpSettings.method = arg1;
}

void StreamSettingsWidget::on_tcpKeepAliveIntervalSpinBox_valueChanged(int arg1)
{
    stream.sockopt.tcpKeepAliveInterval = arg1;
}

void StreamSettingsWidget::on_httpHeadersDefBtn_clicked()
{
    httpHeadersTxt->clear();
    httpHeadersTxt->setPlainText(JsonToString(HttpObject().toJson()["headers"].toObject()));
    stream.httpSettings.headers = HttpObject().headers;
}

void StreamSettingsWidget::on_httpHeadersEditBtn_clicked()
{
    JsonEditor w(JsonFromString(httpHeadersTxt->toPlainText()), this);
    auto rJson = w.OpenEditor();
    httpHeadersTxt->setPlainText(JsonToString(rJson));

    auto json = HttpObject().toJson();
    json["headers"] = rJson;
    stream.httpSettings.headers = HttpObject::fromJson(json).headers;
}