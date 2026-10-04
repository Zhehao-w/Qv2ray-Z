#define CATCH_CONFIG_MAIN
#include "catch.hpp"
#include "ui/widgets/windows/UiPolishPolicy.hpp"

using Qv2ray::ui::polish::ResolveMainWindowStartupLayout;

TEST_CASE("Fresh main window width is added only to the connection pane")
{
    const auto layout = ResolveMainWindowStartupLayout(false, 810, 260, 530);
    REQUIRE(layout.windowWidth == 860);
    REQUIRE(layout.connectionWidth == 310);
    REQUIRE(layout.contentWidth == 530);
}

TEST_CASE("Stored main window width leaves restored geometry and splitter sizes unchanged")
{
    const auto layout = ResolveMainWindowStartupLayout(true, 940, 260, 660);
    REQUIRE(layout.windowWidth == 940);
    REQUIRE(layout.connectionWidth == 260);
    REQUIRE(layout.contentWidth == 660);
}
