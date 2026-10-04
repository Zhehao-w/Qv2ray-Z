#pragma once

namespace Qv2ray::ui::group_manager_safety
{
    enum class ConnectionRemovalEffect
    {
        UnlinkOnly,
        DeletePersistedConnection,
    };

    inline ConnectionRemovalEffect ClassifyConnectionRemoval(int persistedMembershipCount)
    {
        return persistedMembershipCount > 1 ? ConnectionRemovalEffect::UnlinkOnly : ConnectionRemovalEffect::DeletePersistedConnection;
    }

    inline bool RequiresDestructiveRemovalConfirmation(int destructiveConnectionCount)
    {
        return destructiveConnectionCount > 0;
    }
} // namespace Qv2ray::ui::group_manager_safety
