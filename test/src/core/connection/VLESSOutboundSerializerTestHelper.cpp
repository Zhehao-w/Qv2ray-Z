#include "VLESSOutboundSerializerTestHelper.hpp"

#include "base/VLESSShareLinkOpaque.hpp"
#include "plugins/protocols/core/OutboundHandler.hpp"

QString SerializeVLESSOutboundForTest(const QString &alias, const QJsonObject &settings, const QJsonObject &streamSettings)
{
    const auto serialized = BuiltinSerializer().SerializeOutbound("vless", alias, {}, settings, streamSettings);
    return Qv2ray::base::vless_share::AppendOpaqueQueryItems(
        serialized, streamSettings.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray());
}

QPair<QString, int> GetVLESSOutboundHostForTest(const QJsonObject &settings)
{
    const auto info = BuiltinSerializer().GetOutboundInfo("vless", settings);
    return { info.value(Qv2rayPlugin::INFO_SERVER).toString(), info.value(Qv2rayPlugin::INFO_PORT).toInt() };
}

QJsonObject SetVLESSOutboundHostForTest(const QJsonObject &settings, const QString &address, int port)
{
    auto updated = settings;
    Qv2rayPlugin::OutboundInfoObject info;
    info[Qv2rayPlugin::INFO_SERVER] = address;
    info[Qv2rayPlugin::INFO_PORT] = port;
    BuiltinSerializer().SetOutboundInfo("vless", info, updated);
    return updated;
}
