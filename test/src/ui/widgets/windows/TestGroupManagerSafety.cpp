#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "ui/widgets/windows/GroupManagerSafety.hpp"

using namespace Qv2ray::ui::group_manager_safety;

TEST_CASE("group membership removal distinguishes unlink from persisted deletion")
{
    REQUIRE(ClassifyConnectionRemoval(3) == ConnectionRemovalEffect::UnlinkOnly);
    REQUIRE(ClassifyConnectionRemoval(2) == ConnectionRemovalEffect::UnlinkOnly);
    REQUIRE(ClassifyConnectionRemoval(1) == ConnectionRemovalEffect::DeletePersistedConnection);

    // An inconsistent zero-membership observation must fail toward the safer,
    // destructive classification rather than silently bypass confirmation.
    REQUIRE(ClassifyConnectionRemoval(0) == ConnectionRemovalEffect::DeletePersistedConnection);
}

TEST_CASE("bulk removal asks before any batch containing persisted deletion")
{
    REQUIRE_FALSE(RequiresDestructiveRemovalConfirmation(0));
    REQUIRE(RequiresDestructiveRemovalConfirmation(1));
    REQUIRE(RequiresDestructiveRemovalConfirmation(4));
}
