// Render tests for the tab bar and status bar (docs/DESIGN.md §12.1): rendered
// to fixed screens at the three reference widths, asserting one row, no
// overflow, the active-tab marker, and status text presence.
#include "tui/src/render_util.hpp"
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

#include <algorithm>  // std::count
#include <array>
#include <optional>
#include <string>

#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>  // string_width

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

// ---------------------------------------------------------------------------
// R2 (task 10.1): the active tab is unmistakable, with or without colour.
// ---------------------------------------------------------------------------

TEST(TabBarRender, ActiveTabIsAccentInFullNeverYellow) {
    const Theme theme(ColorMode::Full, false);
    for (int tab = 0; tab < 4; ++tab) {
        ftxui::Screen screen = renderTo(views::renderTabBar(tab, theme), {120, 1});
        bool anyAccent = false;
        bool anyYellow = false;
        for (int x = 0; x < screen.dimx(); ++x) {
            const ftxui::Pixel& p = screen.PixelAt(x, 0);
            if (p.foreground_color == ftxui::Color::Cyan) anyAccent = true;
            if (p.foreground_color == ftxui::Color::Yellow) anyYellow = true;
        }
        EXPECT_TRUE(anyAccent) << "tab " << tab;
        // Yellow is the risk role; "current mode" is not a risk (design D6).
        EXPECT_FALSE(anyYellow) << "tab " << tab;
    }
}

TEST(TabBarRender, ActiveTabCarriesAnAsciiMarkerInMonoAndPlain) {
    const std::array<const char*, 4> labels{{"Devices", "Modules", "Updates", "Snapshots"}};
    const std::array<const char*, 4> keys{{"1", "2", "3", "4"}};
    for (const ColorMode mode : {ColorMode::Mono, ColorMode::Plain}) {
        const Theme theme(mode, false);
        for (int tab = 0; tab < 4; ++tab) {
            ftxui::Screen screen = renderTo(views::renderTabBar(tab, theme), {120, 1});
            const std::string row = rowText(screen, 0);
            // Exactly one tab wears the marker, and it is the active one.
            EXPECT_NE(row.find("{" + std::string(keys[static_cast<std::size_t>(tab)]) + "}"),
                      std::string::npos)
                << labels[static_cast<std::size_t>(tab)];
            EXPECT_EQ(std::count(row.begin(), row.end(), '{'), 1)
                << labels[static_cast<std::size_t>(tab)];
            // Every other tab keeps the plain bracket.
            EXPECT_EQ(std::count(row.begin(), row.end(), '['), 3)
                << labels[static_cast<std::size_t>(tab)];
            // ASCII only, and the bar's geometry is unchanged by the marker.
            for (const unsigned char c : row) EXPECT_LT(c, 0x80U) << row;
        }
    }
}

TEST(TabBarRender, ActiveMarkerGeometryMatchesTheInactiveBracket) {
    const Theme theme(ColorMode::Mono, false);
    const std::string first = rowText(renderTo(views::renderTabBar(0, theme), {120, 1}), 0);
    const std::string last = rowText(renderTo(views::renderTabBar(3, theme), {120, 1}), 0);
    EXPECT_EQ(first.size(), last.size());  // no column shift when the tab changes
}

// 10.5: the three non-colour markers must be three different glyphs on a real
// screen, so a Mono/Plain terminal is unambiguous (ui-accessibility "Markers are
// distinct"). Rendered together: the active-tab bracket, the selection cursor
// and the criticality badge.
TEST(MarkerDistinctness, ActiveTabFocusAndCriticalityAreThreeGlyphs) {
    for (const ColorMode mode : {ColorMode::Mono, ColorMode::Plain}) {
        const Theme theme(mode, false);
        ftxui::Screen screen =
            renderTo(ftxui::vbox({
                         views::renderTabBar(1, theme),
                         render::menuRow("amdgpu", /*selected=*/true, /*listFocused=*/true,
                                         std::nullopt, std::nullopt, theme,
                                         render::Badge{render::Glyph::Essential, Role::Warning}),
                         render::menuRow("i915", false, false, std::nullopt, std::nullopt, theme),
                     }),
                     {120, 3});
        const std::string tabs = rowText(screen, 0);
        const std::string selected = rowText(screen, 1);
        const std::string ordinary = rowText(screen, 2);

        EXPECT_NE(tabs.find("{2}"), std::string::npos) << tabs;        // active tab
        EXPECT_EQ(selected.substr(0, 2), "> ");                        // focus cursor
        EXPECT_NE(selected.find('#'), std::string::npos) << selected;  // criticality
        // Each marker appears only where it belongs.
        EXPECT_EQ(tabs.find('#'), std::string::npos) << tabs;
        EXPECT_EQ(selected.find('{'), std::string::npos) << selected;
        EXPECT_EQ(ordinary.find('#'), std::string::npos) << ordinary;
        EXPECT_NE(ordinary.substr(0, 2), "> ");
    }
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

// ---------------------------------------------------------------------------
// B6 (task 8.6): the status line is exactly one row, whatever it is given.
// ---------------------------------------------------------------------------

TEST(StatusBarRender, LongMessageOccupiesExactlyOneRowAtEightyColumns) {
    const Theme theme(ColorMode::Full, false);
    const std::string longStatus =
        "Refused: firmware update for Dell Dock WD19TB requires a reboot and the device must stay "
        "connected to mains power for the whole flash, which takes about eight minutes";
    ASSERT_GT(longStatus.size(), 120U);
    for (Size s : kSizes) {
        // Rendered into a taller screen than the bar needs, so a bar that wrapped
        // would visibly occupy a second row.
        ftxui::Screen screen = renderTo(views::renderStatusBar(longStatus, Role::Danger, theme), s);
        int rowsWithText = 0;
        for (int y = 0; y < screen.dimy(); ++y) {
            if (!rowText(screen, y).empty()) ++rowsWithText;
        }
        EXPECT_EQ(rowsWithText, 1) << "status rows @" << s.w;
        EXPECT_LE(ftxui::string_width(rowText(screen, 0)), s.w) << "status width @" << s.w;
        // Right-elided: the head of the message survives, the tail does not,
        // and the ellipsis says so rather than the sentence just stopping.
        EXPECT_NE(rowText(screen, 0).find("Refused: firmware update"), std::string::npos)
            << "@" << s.w;
        EXPECT_EQ(rowText(screen, 0).find("eight minutes"), std::string::npos) << "@" << s.w;
        EXPECT_NE(rowText(screen, 0).find("…"), std::string::npos) << "ellipsis @" << s.w;
    }
}

// A ViewModel message may carry an embedded newline (a wrapped daemon error,
// say). The bar is one row whatever it is handed, so the control character is
// flattened rather than allowed to move the screen edge.
TEST(StatusBarRender, EmbeddedNewlineStaysOnOneRow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(views::renderStatusBar("Error: restore failed\n  see journalctl -u devmgrd",
                                        Role::Danger, theme),
                 {80, 3});
    int rowsWithText = 0;
    for (int y = 0; y < screen.dimy(); ++y) {
        if (!rowText(screen, y).empty()) ++rowsWithText;
    }
    EXPECT_EQ(rowsWithText, 1);
    EXPECT_NE(rowText(screen, 0).find("see journalctl"), std::string::npos);
}

// Plain mode emits no non-ASCII byte, the truncation marker included.
TEST(StatusBarRender, PlainModeElidesWithAsciiDots) {
    const Theme plain(ColorMode::Plain, false);
    const std::string longStatus(200, 'x');
    ftxui::Screen screen =
        renderTo(views::renderStatusBar(longStatus, std::nullopt, plain), {80, 1});
    const std::string row = rowText(screen, 0);
    EXPECT_NE(row.find("..."), std::string::npos);
    EXPECT_EQ(row.find("…"), std::string::npos);
    for (const unsigned char c : row) EXPECT_LT(c, 0x80U) << row;
}

// The bar keeps its full-width reverse-video background even when the message
// is short, so the screen edge stays stable (DESIGN.md §3.2).
TEST(StatusBarRender, ShortMessageStillFillsTheRow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(views::renderStatusBar("Enabled eth0", Role::Success, theme), {80, 3});
    int inverted = 0;
    for (int x = 0; x < screen.dimx(); ++x) {
        if (screen.PixelAt(x, 0).inverted) ++inverted;
    }
    EXPECT_EQ(inverted, 80);
}

}  // namespace
}  // namespace devmgr::tui
