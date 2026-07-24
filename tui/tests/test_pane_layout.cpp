// Master-detail split for the three wide views (task 12.3b follow-on).
//
// Modules, Updates and Snapshots pinned their left pane at a hard 72 columns.
// At the DESIGN §3.2 minimum of 80 that left the detail pane SIX columns — two
// of them its own border — so every detail line rendered as three characters and
// an ellipsis. The pane was present and empty, which is how the R4 pairing
// (marker glyph on the row, criticality WORD in the detail pane, both on one
// screen) stopped holding at 80x24 while the render tests, all of which ran the
// wide views at 120 columns, stayed green.
//
// Two halves here: the pure width policy, and a render proof that all three
// wide views actually carry detail text at 80x24 now.
#include "tui/src/views/pane_layout.hpp"

#include <array>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>

#include "tui/src/theme.hpp"
#include "tui/src/views/detail_pane.hpp"
#include "tui/src/views/modules_view.hpp"
#include "tui/src/views/snapshots_view.hpp"
#include "tui/src/views/updates_view.hpp"

namespace devmgr::tui {
namespace {

// The three reference widths, plus the point where the preferred width returns.
constexpr int kMinCols = 80;
constexpr int kPreferredPane = 72;
constexpr int kFloorPane = 44;  // == the Devices pane

TEST(PaneLayout, KeepsThePreferredWidthWhenTheTerminalCanAffordIt) {
    EXPECT_EQ(views::wideLeftPaneWidth(120), kPreferredPane);
    EXPECT_EQ(views::wideLeftPaneWidth(108), kPreferredPane);
    // 108 is the first width that affords both the preferred pane and the
    // detail floor; one column narrower and the list starts yielding.
    EXPECT_LT(views::wideLeftPaneWidth(107), kPreferredPane);
}

// The whole point: whatever the width, the detail pane keeps enough columns to
// render a line rather than an ellipsis.
TEST(PaneLayout, DetailPaneNeverFallsBelowItsFloor) {
    for (int cols = kMinCols; cols <= 200; ++cols) {
        const int detail = cols - 2 - views::wideLeftPaneWidth(cols);
        EXPECT_GE(detail, 34) << "@" << cols;
    }
}

TEST(PaneLayout, AtEightyColumnsBothPanesMatchTheDevicesSplit) {
    EXPECT_EQ(views::wideLeftPaneWidth(kMinCols), kFloorPane);
    EXPECT_EQ(kMinCols - 2 - views::wideLeftPaneWidth(kMinCols), 34);
}

// Below the DESIGN §3.2 minimum the min-size notice takes the screen, so this
// is defensive only: the function stays total and never returns a width that
// would make the split nonsensical.
TEST(PaneLayout, ClampsBelowTheMinimumInsteadOfGoingNegative) {
    for (const int cols : {0, 1, 40, 79}) {
        EXPECT_EQ(views::wideLeftPaneWidth(cols), kFloorPane) << "@" << cols;
    }
}

TEST(PaneLayout, WidensMonotonicallyWithTheTerminal) {
    int previous = 0;
    for (int cols = kMinCols; cols <= 140; ++cols) {
        const int pane = views::wideLeftPaneWidth(cols);
        EXPECT_GE(pane, previous) << "@" << cols;
        previous = pane;
    }
}

// ---------------------------------------------------------------------------
// Render proof at the size that was broken.
// ---------------------------------------------------------------------------

ftxui::Screen renderTo(const ftxui::Element& el, int w, int h) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(w), ftxui::Dimension::Fixed(h));
    ftxui::Render(screen, el);
    return screen;
}

// Text right of the list pane and its border — i.e. the detail side only.
bool detailPaneContains(const ftxui::Screen& screen, const std::string& needle, int paneWidth) {
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string row;
        for (int x = paneWidth + 2; x < screen.dimx(); ++x) row += screen.PixelAt(x, y).character;
        if (row.find(needle) != std::string::npos) return true;
    }
    return false;
}

TEST(PaneLayout, AllThreeWideViewsCarryDetailTextAtEightyByTwentyFour) {
    using namespace ftxui;
    const Theme theme(ColorMode::Full, false);
    const int pane = views::wideLeftPaneWidth(kMinCols);

    // A word long enough that the old six-column pane could only have shown
    // "unl…", and short enough to be unambiguous when found.
    const std::vector<std::string> lines{"Module:  amdgpu",
                                         "Risk:    essential — unloading breaks the session"};

    ftxui::Screen modules =
        renderTo(views::renderModulesView({.activeTab = 1,
                                           .banner = "Secure Boot: enabled",
                                           .filterInput = text("filter"),
                                           .list = vbox({text("amdgpu")}),
                                           .detail = views::renderDetailPane(lines, theme),
                                           .statusText = "ok",
                                           .leftPaneWidth = pane},
                                          theme),
                 kMinCols, 24);
    EXPECT_TRUE(detailPaneContains(modules, "essential", pane));

    ftxui::Screen updates = renderTo(
        views::renderUpdatesView({.activeTab = 2,
                                  .banner = "fwupd",
                                  .list = vbox({text("nvme0")}),
                                  .detail = views::renderDetailPane({"Version: 1.2.3"}, theme),
                                  .statusText = "ok",
                                  .leftPaneWidth = pane},
                                 theme),
        kMinCols, 24);
    EXPECT_TRUE(detailPaneContains(updates, "Version", pane));

    ftxui::Screen snapshots = renderTo(
        views::renderSnapshotsView({.activeTab = 3,
                                    .banner = "2 snapshots",
                                    .statusText = "ok",
                                    .leftPaneWidth = pane,
                                    .filterInput = text("filter"),
                                    .list = vbox({text("snap-1")}),
                                    .detail = views::renderDetailPane({"Health:  healthy"}, theme)},
                                   theme),
        kMinCols, 24);
    EXPECT_TRUE(detailPaneContains(snapshots, "healthy", pane));
}

}  // namespace
}  // namespace devmgr::tui
