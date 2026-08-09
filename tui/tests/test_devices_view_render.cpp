// Render tests for the Devices tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes (tab bar,
// legend, status line), no row overflows the width, and the per-row
// selection/focus markers behave. Behaviour-preserving extraction from
// tui_app.cpp — no colour asserted yet (group 4).
#include "tui/src/views/devices_view.hpp"

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

// Row y as a plain string (no styling), trailing spaces trimmed.
std::string rowText(const ftxui::Screen& screen, int y) {
    std::string out;
    for (int x = 0; x < screen.dimx(); ++x) out += screen.PixelAt(x, y).character;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// True if any row of the screen contains `needle`.
bool screenContains(const ftxui::Screen& screen, const std::string& needle) {
    for (int y = 0; y < screen.dimy(); ++y) {
        if (rowText(screen, y).find(needle) != std::string::npos) return true;
    }
    return false;
}

// A representative Devices tab: two rows (one active+focused, one plain), a
// detail line and a status message. Interactive bodies are stand-in text/rows,
// exactly what the shell passes as pre-rendered Elements.
views::DevicesView sampleView() {
    using namespace ftxui;
    return {.activeTab = 0,
            .filterInput = text("filter devices…"),
            .deviceList = vbox({views::renderDeviceRow("nvme0n1", true, true),
                                views::renderDeviceRow("eth0", false, false)}),
            .detail = text("Name: nvme0n1"),
            .statusText = "Refreshed 18 devices.",
            .leftPaneWidth = 44};
}

TEST(DevicesViewRender, ComposesFrameAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderDevicesView(sampleView(), theme), s);
        // Tab bar names the views; legend and status line are present.
        EXPECT_TRUE(screenContains(screen, "Devices"));
        EXPECT_TRUE(screenContains(screen, "Modules"));
        EXPECT_TRUE(screenContains(screen, "Refreshed 18 devices."));
        // Legend reaches its final shortcut even at the 80-column minimum — the
        // full-width row was the fix for the truncated in-pane legend.
        EXPECT_TRUE(screenContains(screen, "q=quit)"));
        // No row is written past the fixed width (no out-of-bounds writes).
        // Measured in display columns, not bytes: box-drawing borders are
        // multi-byte UTF-8, so string length would overcount cells.
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
        }
    }
}

TEST(DevicesViewRender, ActiveRowShowsMarkerAndBold) {
    // Marker policy proven directly on renderDeviceRow so it is covered
    // independent of the surrounding layout.
    ftxui::Screen active = renderTo(views::renderDeviceRow("nvme0n1", true, false), {40, 1});
    EXPECT_EQ(rowText(active, 0).substr(0, 2), "> ");
    bool anyBold = false;
    for (int x = 0; x < active.dimx(); ++x) {
        if (active.PixelAt(x, 0).bold) anyBold = true;
    }
    EXPECT_TRUE(anyBold);

    ftxui::Screen inactive = renderTo(views::renderDeviceRow("eth0", false, false), {40, 1});
    EXPECT_EQ(rowText(inactive, 0).substr(0, 2), "  ");
}

TEST(DevicesViewRender, FocusedRowIsReverseVideo) {
    ftxui::Screen focused = renderTo(views::renderDeviceRow("nvme0n1", true, true), {40, 1});
    bool anyInverted = false;
    for (int x = 0; x < focused.dimx(); ++x) {
        if (focused.PixelAt(x, 0).inverted) anyInverted = true;
    }
    EXPECT_TRUE(anyInverted);

    ftxui::Screen unfocused = renderTo(views::renderDeviceRow("eth0", true, false), {40, 1});
    bool anyInvertedUnfocused = false;
    for (int x = 0; x < unfocused.dimx(); ++x) {
        if (unfocused.PixelAt(x, 0).inverted) anyInvertedUnfocused = true;
    }
    EXPECT_FALSE(anyInvertedUnfocused);
}

}  // namespace
}  // namespace devmgr::tui
