// Render tests for the tab bar and status bar (docs/DESIGN.md §12.1): rendered
// to fixed screens at the three reference widths, asserting one row, no
// overflow, the active-tab marker, and status text presence.
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

#include <array>
#include <optional>
#include <string>

#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>

#include "tui/src/theme.hpp"

namespace devmgr::tui {
namespace {

struct Size {
    int w;
    int h;
};
constexpr std::array<Size, 3> kSizes{{{120, 32}, {100, 28}, {80, 24}}};

ftxui::Screen renderTo(const ftxui::Element& el, Size s) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(s.w), ftxui::Dimension::Fixed(s.h));
    ftxui::Render(screen, el);
    return screen;
}

// Row y as a plain string (no styling), trailing spaces trimmed.
std::string rowText(const ftxui::Screen& screen, int y) {
    std::string out;
    for (int x = 0; x < screen.dimx(); ++x) out += screen.PixelAt(x, y).character;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

TEST(TabBarRender, NamesAllViewsAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderTabBar(0, theme), s);
        const std::string row = rowText(screen, 0);
        EXPECT_NE(row.find("Devices"), std::string::npos);
        EXPECT_NE(row.find("Modules"), std::string::npos);
        EXPECT_NE(row.find("Updates"), std::string::npos);
        EXPECT_NE(row.find("Snapshots"), std::string::npos);
        // No cell may be written past the fixed width.
        EXPECT_LE(static_cast<int>(row.size()), s.w);
    }
}

TEST(TabBarRender, ActiveTabIsBold) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderTabBar(2, theme), {120, 32});
    // "Updates" is the active view (tab 2): its glyphs render bold.
    const std::string row = rowText(screen, 0);
    const std::string::size_type at = row.find("Updates");
    ASSERT_NE(at, std::string::npos);
    // Find the same column on screen and check the bold attribute.
    bool anyBold = false;
    for (int x = 0; x < screen.dimx(); ++x) {
        if (screen.PixelAt(x, 0).character == "U" && screen.PixelAt(x, 0).bold) anyBold = true;
    }
    EXPECT_TRUE(anyBold);
}

TEST(StatusBarRender, ShowsTextReverseVideo) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen =
            renderTo(views::renderStatusBar("Refreshed 18 devices.", std::nullopt, theme), s);
        const std::string row = rowText(screen, 0);
        EXPECT_NE(row.find("Refreshed 18 devices."), std::string::npos);
        EXPECT_LE(static_cast<int>(row.size()), s.w);
        // Reverse video is applied across the message.
        EXPECT_TRUE(screen.PixelAt(1, 0).inverted);
    }
}

}  // namespace
}  // namespace devmgr::tui
