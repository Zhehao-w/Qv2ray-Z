#include "VLESSOutboundSerializerTestHelper.hpp"

#include "base/VLESSShareLinkOpaque.hpp"
#include "plugins/protocols/core/OutboundHandler.hpp"

#include <QJsonArray>

QString SerializeVLESSOutboundForTest(const QString &alias, const QJsonObject &settings, const QJsonObject &streamSettings)
{
    QJsonObject normalizedSettings = settings;
    const auto vnext = settings["vnext"].toArray();
    if (!vnext.isEmpty())
    {
        const auto server = VLESSServerObject::fromJson(vnext.first().toObject());
        normalizedSettings["vnext"] = QJsonArray{ server.toJson() };
    }
    const auto serialized = BuiltinSerializer().SerializeOutbound("vless", alias, {}, normalizedSettings, streamSettings);
    return Qv2ray::base::vless_share::AppendOpaqueQueryItems(
        serialized, streamSettings.value(Qv2ray::base::vless_share::OpaqueQueryMetadataKey()).toArray());
}
