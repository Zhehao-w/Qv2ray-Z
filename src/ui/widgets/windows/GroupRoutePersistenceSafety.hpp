#pragma once

namespace Qv2ray::ui::group_manager_safety
{
    inline bool GroupRouteSettersAccepted(bool dnsAccepted, bool routeAccepted)
    {
        return dnsAccepted && routeAccepted;
    }
} // namespace Qv2ray::ui::group_manager_safety
