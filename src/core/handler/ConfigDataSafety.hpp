#pragma once

#include "base/models/QvConfigIdentifier.hpp"

namespace Qv2ray::core::handler::data_safety
{
    struct SubscriptionMembershipDelta
    {
        QList<ConnectionId> finalConnections;
        QList<ConnectionId> added;
        QList<ConnectionId> removed;
    };

    inline SubscriptionMembershipDelta BuildSubscriptionMembership(const QList<ConnectionId> &original,
                                                                    const QList<ConnectionId> &replacement,
                                                                    bool removeUnmatched)
    {
        SubscriptionMembershipDelta delta;
        delta.finalConnections = replacement;

        if (!removeUnmatched)
        {
            for (const auto &id : original)
            {
                if (!delta.finalConnections.contains(id))
                    delta.finalConnections.append(id);
            }
        }

        for (const auto &id : delta.finalConnections)
        {
            if (!original.contains(id) && !delta.added.contains(id))
                delta.added.append(id);
        }

        for (const auto &id : original)
        {
            if (!delta.finalConnections.contains(id) && !delta.removed.contains(id))
                delta.removed.append(id);
        }

        return delta;
    }
} // namespace Qv2ray::core::handler::data_safety
