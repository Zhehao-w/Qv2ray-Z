#pragma once

namespace Qv2ray::ui::polish
{
    struct MainWindowStartupLayout
    {
        int windowWidth;
        int connectionWidth;
        int contentWidth;
    };

    constexpr int MAIN_WINDOW_CONNECTION_WIDTH_INCREMENT = 50;

    constexpr MainWindowStartupLayout ResolveMainWindowStartupLayout(bool hasStoredWindowWidth, int windowWidth, int connectionWidth,
                                                                      int contentWidth)
    {
        if (hasStoredWindowWidth)
            return { windowWidth, connectionWidth, contentWidth };

        return { windowWidth + MAIN_WINDOW_CONNECTION_WIDTH_INCREMENT, connectionWidth + MAIN_WINDOW_CONNECTION_WIDTH_INCREMENT,
                 contentWidth };
    }
} // namespace Qv2ray::ui::polish
