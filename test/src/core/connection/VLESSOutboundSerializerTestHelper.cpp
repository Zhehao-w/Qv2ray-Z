#include "VLESSOutboundSerializerTestHelper.hpp"

#include "base/VLESSShareLinkOpaque.hpp"
#include "plugins/protocols/core/OutboundHandler.hpp"

QString SerializeVLESSOutboundForTest(const QString &alias, const QJsonObject &settings, const QJsonObject &streamSettings)
{
    const auto serialized = BuiltinSerializer().SerializeOutbound("vless", alias, {}, settings, streamSettings);
    return Qv2ray::base::vless_share::AppendOpaqueQueryItems(
        serialized, streamSettings.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray());
}
