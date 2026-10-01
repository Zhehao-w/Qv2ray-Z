#include "catch.hpp"

#include "core/handler/ConnectionContextResolver.hpp"

using Qv2ray::core::handler::connection_context::IsMembership;
using Qv2ray::core::handler::connection_context::Resolve;

TEST_CASE("connection context resolution preserves explicit and active membership")
{
    const ConnectionId connection{ "connection" };
    const GroupId alpha{ "alpha" };
    const GroupId beta{ "beta" };
    const QList<GroupId> memberships{ alpha, beta };

    REQUIRE(Resolve(connection, memberships, { connection, beta }, { connection, alpha }, { connection, alpha }) ==
            ConnectionGroupPair{ connection, beta });
    REQUIRE(Resolve(connection, memberships, {}, { connection, beta }, { connection, alpha }) == ConnectionGroupPair{ connection, beta });
}

TEST_CASE("stale preferred contexts fall through to last connected membership")
{
    const ConnectionId connection{ "connection" };
    const GroupId alpha{ "alpha" };
    const GroupId beta{ "beta" };
    const GroupId removed{ "removed" };

    const auto resolved = Resolve(connection, { alpha, beta }, { connection, removed }, { connection, removed }, { connection, beta });
    REQUIRE(resolved == ConnectionGroupPair{ connection, beta });
    REQUIRE_FALSE(IsMembership({ connection, removed }, { alpha, beta }));
}

TEST_CASE("default group is the stable fallback when it contains the connection")
{
    const ConnectionId connection{ "connection" };
    const GroupId other{ "other" };

    REQUIRE(Resolve(connection, { other, DefaultGroupId }) == ConnectionGroupPair{ connection, DefaultGroupId });
}

TEST_CASE("fallback is deterministic and independent of membership iteration order")
{
    const ConnectionId connection{ "connection" };
    const GroupId alpha{ "alpha" };
    const GroupId beta{ "beta" };
    const GroupId gamma{ "gamma" };

    REQUIRE(Resolve(connection, { gamma, alpha, beta }) == ConnectionGroupPair{ connection, alpha });
    REQUIRE(Resolve(connection, { beta, gamma, alpha }) == ConnectionGroupPair{ connection, alpha });
}

TEST_CASE("resolution fails closed without a usable membership")
{
    const ConnectionId connection{ "connection" };
    const GroupId group{ "group" };

    REQUIRE(Resolve(NullConnectionId, { group }).isEmpty());
    REQUIRE(Resolve(connection, {}).isEmpty());
}
