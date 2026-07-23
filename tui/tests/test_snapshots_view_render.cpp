// Render tests for the Snapshots tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes, the
// restore-preview modal replaces the list body, and the recovery-guidance panel
// appears only when present. Behaviour-preserving extraction from tui_app.cpp —
// no colour asserted yet (group 4).
#include "tui/src/views/snapshots_view.hpp"

#include <array>
#include <string>
#include <vector>

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

views::SnapshotsView masterDetailView() {
    using namespace ftxui;
    views::SnapshotsView v{.activeTab = 3,
                           .banner = "12 snapshots — HEAD @ a1b2c3",
                           .statusText = "Loaded snapshots.",
                           .leftPaneWidth = 72};
    v.filterInput = text("filter snapshots…");
    v.list = vbox({text("a1b2c3  boot"), text("d4e5f6  manual")});
    v.detail = text("Snapshot a1b2c3");
    return v;
}

TEST(SnapshotsViewRender, ComposesFrameAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderSnapshotsView(masterDetailView(), theme), s);
        EXPECT_TRUE(screenContains(screen, "Snapshots"));          // tab bar + legend
        EXPECT_TRUE(screenContains(screen, "HEAD @ a1b2c3"));      // banner
        EXPECT_TRUE(screenContains(screen, "Loaded snapshots."));  // status
        EXPECT_TRUE(screenContains(screen, "q=quit)"));            // full legend
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
        }
    }
}

TEST(SnapshotsViewRender, PreviewModalReplacesListBody) {
    const Theme theme(ColorMode::Full, false);
    views::SnapshotsView v = masterDetailView();
    // The shell leaves the list null in preview mode; assert the modal owns the
    // pane and the underlying list content is not shown.
    v.list = ftxui::text("LIST_SHOULD_BE_HIDDEN");
    v.showPreview = true;
    v.previewLines = {"Restore preview", "current -> a1b2c3", "converges cleanly"};

    ftxui::Screen screen = renderTo(views::renderSnapshotsView(std::move(v), theme), {120, 32});
    EXPECT_TRUE(screenContains(screen, "Restore preview"));
    EXPECT_TRUE(screenContains(screen, "converges cleanly"));
    EXPECT_FALSE(screenContains(screen, "LIST_SHOULD_BE_HIDDEN"));
}

TEST(SnapshotsViewRender, GuidancePanelShownOnlyWhenPresent) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen without =
        renderTo(views::renderSnapshotsView(masterDetailView(), theme), {120, 32});
    EXPECT_FALSE(screenContains(without, "safety snapshot"));

    views::SnapshotsView v = masterDetailView();
    v.guidanceLines = {"Restore did not fully converge.", "safety snapshot: f0e1d2"};
    ftxui::Screen with = renderTo(views::renderSnapshotsView(std::move(v), theme), {120, 32});
    EXPECT_TRUE(screenContains(with, "safety snapshot: f0e1d2"));
}

// B4 (task 8.4): the empty store shows exactly ONE empty indicator. The counts
// banner is empty when there is nothing to count, and an empty banner renders no
// row at all — so the list's "(no snapshots)" placeholder stands alone instead
// of being repeated above the separator.
TEST(SnapshotsViewRender, EmptyStoreShowsASingleEmptyIndicator) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        views::SnapshotsView v = masterDetailView();
        v.banner = "";  // SnapshotsVM::banner() with an empty store
        v.list = ftxui::text("(no snapshots)");
        v.detail = ftxui::text("(no snapshot selected)");
        ftxui::Screen screen = renderTo(views::renderSnapshotsView(std::move(v), theme), s);
        int hits = 0;
        for (int y = 0; y < screen.dimy(); ++y) {
            if (rowText(screen, y).find("no snapshots") != std::string::npos) ++hits;
        }
        EXPECT_EQ(hits, 1) << "empty indicators @" << s.w;
        EXPECT_TRUE(screenContains(screen, "(no snapshots)")) << s.w;
    }
}

// An empty banner costs no row: the frame is one row shorter than the same view
// with a banner, which is what returns the line to the 80x24 budget.
TEST(SnapshotsViewRender, EmptyBannerRendersNoRow) {
    const Theme theme(ColorMode::Full, false);
    views::SnapshotsView withBanner = masterDetailView();
    ftxui::Screen banner =
        renderTo(views::renderSnapshotsView(std::move(withBanner), theme), {120, 32});
    views::SnapshotsView withoutBanner = masterDetailView();
    withoutBanner.banner = "";
    ftxui::Screen blank =
        renderTo(views::renderSnapshotsView(std::move(withoutBanner), theme), {120, 32});
    // The whole body below the banner shifts up exactly one row.
    auto rowOf = [](const ftxui::Screen& s, const std::string& needle) {
        for (int y = 0; y < s.dimy(); ++y) {
            if (rowText(s, y).find(needle) != std::string::npos) return y;
        }
        return -1;
    };
    EXPECT_GE(rowOf(banner, "filter snapshots"), 0);
    EXPECT_EQ(rowOf(blank, "filter snapshots"), rowOf(banner, "filter snapshots") - 1);
    EXPECT_FALSE(screenContains(blank, "12 snapshots"));
}

}  // namespace
}  // namespace devmgr::tui
