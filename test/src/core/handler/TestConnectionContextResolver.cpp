#define CATCH_CONFIG_MAIN
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
    const ConnectionGroupPair betaContext{ connection, beta };

    REQUIRE(Resolve(connection, memberships, { connection, beta }, { connection, alpha }, { connection, alpha }) == betaContext);
    REQUIRE(Resolve(connection, memberships, {}, { connection, beta }, { connection, alpha }) == betaContext);
}

TEST_CASE("stale or mismatched contexts fall through to a valid membership")
{
    const ConnectionId connection{ "connection" };
    const ConnectionId otherConnection{ "other-connection" };
    const GroupId alpha{ "alpha" };
    const GroupId beta{ "beta" };
    const GroupId removed{ "removed" };
    const ConnectionGroupPair betaContext{ connection, beta };

    const auto staleResolved = Resolve(connection, { alpha, beta }, { connection, removed }, { connection, removed }, { connection, beta });
    REQUIRE(staleResolved == betaContext);
    REQUIRE_FALSE(IsMembership({ connection, removed }, { alpha, beta }));

    const auto mismatchedResolved = Resolve(connection, { alpha, beta }, { otherConnection, beta }, { otherConnection, alpha }, betaContext);
    REQUIRE(mismatchedResolved == betaContext);
}

TEST_CASE("default group is the stable fallback when it contains the connection")
{
    const ConnectionId connection{ "connection" };
    const GroupId other{ "other" };
    const ConnectionGroupPair defaultContext{ connection, DefaultGroupId };

    REQUIRE(Resolve(connection, { other, DefaultGroupId }) == defaultContext);
}

TEST_CASE("fallback is deterministic and independent of membership iteration order")
{
    const ConnectionId connection{ "connection" };
    const GroupId alpha{ "alpha" };
    const GroupId beta{ "beta" };
    const GroupId gamma{ "gamma" };
    const ConnectionGroupPair alphaContext{ connection, alpha };

    REQUIRE(Resolve(connection, { gamma, alpha, beta }) == alphaContext);
    REQUIRE(Resolve(connection, { beta, gamma, alpha }) == alphaContext);
}

TEST_CASE("resolution fails closed without a usable membership")
{
    const ConnectionId connection{ "connection" };
    const GroupId group{ "group" };

    REQUIRE(Resolve(NullConnectionId, { group }).isEmpty());
    REQUIRE(Resolve(connection, {}).isEmpty());
}
