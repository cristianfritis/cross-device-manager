// K3 (task 12.1): the minimum-size toggle (DESIGN.md §3.2). Below 80x24 the
// master-detail split cannot fit, so the shell swaps the full UI for a concise
// notice. Both halves of that toggle are pure (views::belowMinimumSize /
// renderMinSizeNotice), so this proves the boundary and the notice off-screen.
#include "tui/src/views/min_size.hpp"

#include <string>

#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>  // string_width

#include <gtest/gtest.h>

namespace devmgr::tui {
namespace {

std::string rowText(const ftxui::Screen& screen, int y) {
    std::string out;
    for (int x = 0; x < screen.dimx(); ++x) out += screen.PixelAt(x, y).character;
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string wholeScreen(const ftxui::Screen& screen) {
    std::string out;
    for (int y = 0; y < screen.dimy(); ++y) out += rowText(screen, y) + "\n";
    return out;
}

ftxui::Screen renderTo(const ftxui::Element& el, int w, int h) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(w), ftxui::Dimension::Fixed(h));
    ftxui::Render(screen, el);
    return screen;
}

// The toggle boundary is exactly 80x24: at or above it the full UI renders,
// below EITHER dimension the notice does.
TEST(MinSizeToggle, BoundaryIsEightyByTwentyFour) {
    EXPECT_EQ(views::kMinCols, 80);
    EXPECT_EQ(views::kMinRows, 24);

    EXPECT_FALSE(views::belowMinimumSize(80, 24));   // exactly the minimum → full UI
    EXPECT_FALSE(views::belowMinimumSize(120, 32));  // comfortably above
    EXPECT_FALSE(views::belowMinimumSize(100, 28));

    EXPECT_TRUE(views::belowMinimumSize(79, 24));  // one column short
    EXPECT_TRUE(views::belowMinimumSize(80, 23));  // one row short
    EXPECT_TRUE(views::belowMinimumSize(79, 23));  // both short
    EXPECT_TRUE(views::belowMinimumSize(0, 0));
}

// The notice names the minimum and both ways out, and fits inside a below-min
// screen without writing past any edge (it is what renders THERE).
TEST(MinSizeToggle, NoticeFitsAndNamesTheMinimumAndTheExits) {
    for (const auto [w, h] : {std::pair{79, 24}, std::pair{80, 23}, std::pair{40, 10}}) {
        ftxui::Screen screen = renderTo(views::renderMinSizeNotice(), w, h);
        const std::string all = wholeScreen(screen);
        EXPECT_NE(all.find("Terminal too small"), std::string::npos) << all;
        EXPECT_NE(all.find("Minimum size is 80x24."), std::string::npos) << all;
        EXPECT_NE(all.find("press q to quit"), std::string::npos) << all;
        for (int y = 0; y < screen.dimy(); ++y)
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), w) << "row " << y << " @" << w;
    }
}

// A too-small terminal is not a coloured state — the notice is plain ASCII.
TEST(MinSizeToggle, NoticeIsAsciiOnly) {
    ftxui::Screen screen = renderTo(views::renderMinSizeNotice(), 79, 24);
    for (int y = 0; y < screen.dimy(); ++y)
        for (const unsigned char c : rowText(screen, y)) EXPECT_LT(c, 0x80U);
}

}  // namespace
}  // namespace devmgr::tui
