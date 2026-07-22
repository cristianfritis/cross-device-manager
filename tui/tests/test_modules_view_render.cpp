// Render tests for the Modules tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes (tab bar,
// legend, banner, status line) and no row overflows the width. Behaviour-
// preserving extraction from tui_app.cpp — no colour asserted yet (group 4).
#include "tui/src/views/modules_view.hpp"

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

}  // namespace
}  // namespace devmgr::tui
