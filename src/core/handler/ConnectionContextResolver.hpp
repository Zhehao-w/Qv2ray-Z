#pragma once

#include "core/CoreUtils.hpp"

#include <algorithm>

namespace Qv2ray::core::handler::connection_context
{
    inline bool IsMembership(const ConnectionGroupPair &pair, const QList<GroupId> &memberships)
    {
        return !pair.isEmpty() && memberships.contains(pair.groupId);
    }

    inline ConnectionGroupPair Resolve(const ConnectionId &connectionId, QList<GroupId> memberships,
                                       const ConnectionGroupPair &preferred = {}, const ConnectionGroupPair &current = {},
                                       const ConnectionGroupPair &lastConnected = {})
    {
        if (connectionId == NullConnectionId || memberships.isEmpty())
            return {};

        const auto matches = [&](const ConnectionGroupPair &candidate) {
            return candidate.connectionId == connectionId && IsMembership(candidate, memberships);
        };

        if (matches(preferred))
            return preferred;
        if (matches(current))
            return current;
        if (matches(lastConnected))
            return lastConnected;

        if (memberships.contains(DefaultGroupId))
            return { connectionId, DefaultGroupId };

        std::sort(memberships.begin(), memberships.end(), [](const GroupId &left, const GroupId &right) {
            return left.toString() < right.toString();
        });
        return { connectionId, memberships.first() };
    }
} // namespace Qv2ray::core::handler::connection_context
