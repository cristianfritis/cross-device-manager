// Render tests for the Updates tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes and that
// the optional request banner appears only when set. Behaviour-preserving
// extraction from tui_app.cpp — no colour asserted yet (group 4).
#include "tui/src/views/updates_view.hpp"

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

views::UpdatesView sampleView(const std::string& requestBanner) {
    using namespace ftxui;
    return {.activeTab = 2,
            .banner = "fwupd 2.0.1 — 3 updates",
            .requestBanner = requestBanner,
            .list = vbox({text("Dock firmware 1.2"), text("SSD firmware 5.0")}),
            .detail = text("Release notes…"),
            .statusText = "Refreshed updates.",
            .leftPaneWidth = 72};
}

TEST(UpdatesViewRender, ComposesFrameAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderUpdatesView(sampleView(""), theme), s);
        EXPECT_TRUE(screenContains(screen, "Updates"));                  // tab bar + legend
        EXPECT_TRUE(screenContains(screen, "fwupd 2.0.1 — 3 updates"));  // banner
        EXPECT_TRUE(screenContains(screen, "Refreshed updates."));       // status
        EXPECT_TRUE(screenContains(screen, "q=quit)"));                  // full legend
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
        }
    }
}

TEST(UpdatesViewRender, RequestBannerShownOnlyWhenSet) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen without = renderTo(views::renderUpdatesView(sampleView(""), theme), {120, 32});
    EXPECT_FALSE(screenContains(without, "reboot required"));

    ftxui::Screen with = renderTo(
        views::renderUpdatesView(sampleView("reboot required to finish"), theme), {120, 32});
    EXPECT_TRUE(screenContains(with, "reboot required to finish"));
}

// ---------------------------------------------------------------------------
// R5 (task 10.4): the Updates list carries the same muted, non-selectable
// column header.
// ---------------------------------------------------------------------------

// Row index of the first row containing `needle` inside the LIST pane (the
// detail pane shares the same rows on the right).
int listRowOf(const ftxui::Screen& screen, const std::string& needle, int paneWidth = 72) {
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string pane;
        for (int x = 0; x < std::min(paneWidth, screen.dimx()); ++x)
            pane += screen.PixelAt(x, y).character;
        if (pane.find(needle) != std::string::npos) return y;
    }
    return -1;
}

views::UpdatesView viewWithHeader() {
    views::UpdatesView v = sampleView("");
    // Exactly what UpdatesVM::columnHeader() emits.
    v.columnHeader = "Src    Device                         Version      -> New";
    return v;
}

TEST(UpdatesViewRender, ColumnHeaderIsMutedNonSelectableAndAboveTheRows) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderUpdatesView(viewWithHeader(), theme), s);
        const int header = listRowOf(screen, "Version", s.w);
        ASSERT_GE(header, 0) << "@" << s.w;
        EXPECT_LT(header, listRowOf(screen, "Dock firmware", s.w)) << "@" << s.w;
        // Muted is dim, never a hue; and the header can never take the cursor.
        bool anyDim = false;
        for (int x = 0; x < screen.dimx(); ++x) {
            if (screen.PixelAt(x, header).dim) anyDim = true;
            EXPECT_FALSE(screen.PixelAt(x, header).inverted) << "@" << s.w;
        }
        EXPECT_TRUE(anyDim) << "@" << s.w;
        // No cursor at the start of the pane. (A whole-row search for "> " would
        // match the header's own "->" arrow column.)
        EXPECT_NE(screen.PixelAt(1, header).character, ">") << "@" << s.w;
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
        }
    }
}

TEST(UpdatesViewRender, EightyByTwentyFourBudgetHoldsWithTheHeader) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderUpdatesView(viewWithHeader(), theme), {80, 24});
    EXPECT_TRUE(screenContains(screen, "Updates"));             // tab bar + legend
    EXPECT_TRUE(screenContains(screen, "fwupd 2.0.1"));         // banner
    EXPECT_TRUE(screenContains(screen, "Version"));             // header
    EXPECT_TRUE(screenContains(screen, "Dock firmware 1.2"));   // >= 1 data row
    EXPECT_TRUE(screenContains(screen, "SSD firmware 5.0"));    // and a second
    EXPECT_TRUE(screenContains(screen, "Refreshed updates."));  // status line
}

TEST(UpdatesViewRender, NoHeaderStringRendersNoHeaderRow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen with = renderTo(views::renderUpdatesView(viewWithHeader(), theme), {120, 32});
    ftxui::Screen without = renderTo(views::renderUpdatesView(sampleView(""), theme), {120, 32});
    EXPECT_EQ(listRowOf(without, "Version"), -1);
    // The header costs two rows here (header + rule), since the Updates pane has
    // no filter field to separate it from.
    EXPECT_LT(listRowOf(without, "Dock firmware"), listRowOf(with, "Dock firmware"));
}

}  // namespace
}  // namespace devmgr::tui
