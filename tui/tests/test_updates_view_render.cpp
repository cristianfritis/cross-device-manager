// Render tests for the Updates tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes and that
// the optional request banner appears only when set. Behaviour-preserving
// extraction from tui_app.cpp — no colour asserted yet (group 4).
#include "tui/src/views/updates_view.hpp"

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

}  // namespace
}  // namespace devmgr::tui
