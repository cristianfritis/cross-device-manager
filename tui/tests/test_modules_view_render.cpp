// Render tests for the Modules tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes (tab bar,
// legend, banner, status line) and no row overflows the width. Behaviour-
// preserving extraction from tui_app.cpp — no colour asserted yet (group 4).
#include "tui/src/views/modules_view.hpp"

#include <algorithm>  // std::min
#include <array>
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

std::string rowText(const ftxui::Screen& screen, int y) {
    std::string out;
    for (int x = 0; x < screen.dimx(); ++x) out += screen.PixelAt(x, y).character;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool screenContains(const ftxui::Screen& screen, const std::string& needle) {
    for (int y = 0; y < screen.dimy(); ++y) {
        if (rowText(screen, y).find(needle) != std::string::npos) return true;
    }
    return false;
}

views::ModulesView sampleView() {
    using namespace ftxui;
    return {.activeTab = 1,
            .banner = "Secure Boot: enabled",
            .filterInput = text("filter modules…"),
            .list = vbox({text("nvidia"), text("i915")}),
            .detail = text("Module: nvidia"),
            .statusText = "Loaded 42 modules.",
            .leftPaneWidth = 72};
}

TEST(ModulesViewRender, ComposesFrameAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderModulesView(sampleView(), theme), s);
        EXPECT_TRUE(screenContains(screen, "Modules"));               // tab bar + legend
        EXPECT_TRUE(screenContains(screen, "Secure Boot: enabled"));  // banner
        EXPECT_TRUE(screenContains(screen, "Loaded 42 modules."));    // status
        EXPECT_TRUE(screenContains(screen, "q=quit)"));               // full legend
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
        }
    }
}

// ---------------------------------------------------------------------------
// R5 (task 10.4): one muted, non-selectable column header, and the 80x24 row
// budget still holds with it.
// ---------------------------------------------------------------------------

// Row index of the first row containing `needle` INSIDE THE LIST PANE. The
// detail pane repeats module names on the same rows, so a whole-row scan would
// match the wrong side of the split.
int listRowOf(const ftxui::Screen& screen, const std::string& needle, int paneWidth = 72) {
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string pane;
        for (int x = 0; x < std::min(paneWidth, screen.dimx()); ++x)
            pane += screen.PixelAt(x, y).character;
        if (pane.find(needle) != std::string::npos) return y;
    }
    return -1;
}

views::ModulesView viewWithHeader() {
    views::ModulesView v = sampleView();
    // Exactly what ModulesVM::columnHeader() emits.
    v.columnHeader = "Name                         Signed                Ref     Size  Used-by";
    return v;
}

TEST(ModulesViewRender, ColumnHeaderIsPresentMutedAndAboveTheRows) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderModulesView(viewWithHeader(), theme), s);
        const int header = listRowOf(screen, "Signed", s.w);
        ASSERT_GE(header, 0) << "@" << s.w;
        EXPECT_LT(header, listRowOf(screen, "i915", s.w)) << "@" << s.w;
        // Muted renders dim with the default foreground — never a hue.
        bool anyDim = false;
        for (int x = 0; x < screen.dimx(); ++x) {
            if (screen.PixelAt(x, header).dim) anyDim = true;
        }
        EXPECT_TRUE(anyDim) << "@" << s.w;
        // Exactly one header row. ("Used-by" is the cell the 72-column pane
        // clips first — the same drop order the data rows follow — so "Signed",
        // the column R5 exists to surface, is what is counted.)
        int headers = 0;
        for (int y = 0; y < screen.dimy(); ++y) {
            std::string pane;
            for (int x = 0; x < std::min(s.w, screen.dimx()); ++x)
                pane += screen.PixelAt(x, y).character;
            if (pane.find("Signed") != std::string::npos) ++headers;
        }
        EXPECT_EQ(headers, 1) << "@" << s.w;
    }
}

// The header is not a list entry, so it can never take the cursor: no "> "
// marker and no reverse video on its row.
TEST(ModulesViewRender, ColumnHeaderCannotTakeTheCursor) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderModulesView(viewWithHeader(), theme), {120, 32});
    const int header = listRowOf(screen, "Signed");
    ASSERT_GE(header, 0);
    EXPECT_EQ(rowText(screen, header).find("> "), std::string::npos);
    for (int x = 0; x < screen.dimx(); ++x) EXPECT_FALSE(screen.PixelAt(x, header).inverted);
}

// 80x24 budget: tab bar, legend, banner, header, at least one data row and the
// status line all fit, and nothing overflows the width.
TEST(ModulesViewRender, EightyByTwentyFourBudgetHoldsWithTheHeader) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderModulesView(viewWithHeader(), theme), {80, 24});
    EXPECT_TRUE(screenContains(screen, "Modules"));               // tab bar + legend
    EXPECT_TRUE(screenContains(screen, "Secure Boot: enabled"));  // banner
    EXPECT_TRUE(screenContains(screen, "Signed"));                // header
    EXPECT_TRUE(screenContains(screen, "nvidia"));                // >= 1 data row
    EXPECT_TRUE(screenContains(screen, "i915"));                  // and a second
    EXPECT_TRUE(screenContains(screen, "Loaded 42 modules."));    // status line
    for (int y = 0; y < screen.dimy(); ++y) {
        EXPECT_LE(ftxui::string_width(rowText(screen, y)), 80);
    }
}

// Mono/Plain: plain muted text, no colour dependence.
TEST(ModulesViewRender, ColumnHeaderIsPlainTextWithoutColour) {
    for (const ColorMode mode : {ColorMode::Mono, ColorMode::Plain}) {
        const Theme theme(mode, false);
        ftxui::Screen screen =
            renderTo(views::renderModulesView(viewWithHeader(), theme), {120, 32});
        const int header = listRowOf(screen, "Signed");
        ASSERT_GE(header, 0);
        for (int x = 0; x < screen.dimx(); ++x) {
            EXPECT_EQ(screen.PixelAt(x, header).foreground_color, ftxui::Color::Default);
            EXPECT_FALSE(screen.PixelAt(x, header).dim);  // decorators are identity here
        }
    }
}

// No header string means no header row at all — the list starts one row higher.
TEST(ModulesViewRender, NoHeaderStringRendersNoHeaderRow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen with = renderTo(views::renderModulesView(viewWithHeader(), theme), {120, 32});
    ftxui::Screen without = renderTo(views::renderModulesView(sampleView(), theme), {120, 32});
    EXPECT_EQ(listRowOf(without, "Signed"), -1);
    EXPECT_EQ(listRowOf(without, "i915"), listRowOf(with, "i915") - 1);
}

}  // namespace
}  // namespace devmgr::tui
