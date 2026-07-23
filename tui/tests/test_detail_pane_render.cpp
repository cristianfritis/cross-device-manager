// R1 (task 11.1): the detail pane's values are bounded to their own row.
//
// The pane shows identity rows a device cannot be identified without — the
// canonical Name, the kernel Address, VID:PID, the app's Id — plus values that
// are routinely longer than the pane is wide (modalias, sysfs path, signer). A
// plain ftxui::text runs such a value to the screen edge and stops mid-token
// with nothing to say it was cut; these tests pin the bounded behaviour at
// 120x32 / 100x28 / 80x24.
#include "tui/src/views/detail_pane.hpp"

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

// The identity block DeviceDetailVM emits, in order.
std::vector<std::string> deviceDetailLines() {
    return {
        "Name:     AMD USB controller",
        "Address:  0000:c5:00.4",
        "VID:PID:  1022:15b8",
        "Id:       pci-0000:c5:00.4",
        "Bus:      PCI",
        "Status:   enabled",
        "Sysfs:    /sys/devices/pci0000:c0/0000:c0:08.3/0000:c5:00.4",
        "Modalias: pci:v00001022d000015B8sv00001022sd000015B8bc0Csc03i30",
    };
}

TEST(DetailPaneRender, ShowsTheIdentityRowsInOrder) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderDetailPane(deviceDetailLines(), theme), s);
        EXPECT_NE(rowText(screen, 0).find("Name:"), std::string::npos) << "@" << s.w;
        EXPECT_NE(rowText(screen, 1).find("Address:"), std::string::npos) << "@" << s.w;
        EXPECT_NE(rowText(screen, 2).find("VID:PID:"), std::string::npos) << "@" << s.w;
        EXPECT_NE(rowText(screen, 3).find("Id:"), std::string::npos) << "@" << s.w;
        // The canonical name is the label, not the address (R1).
        EXPECT_NE(rowText(screen, 0).find("AMD USB controller"), std::string::npos) << "@" << s.w;
        EXPECT_NE(rowText(screen, 1).find("0000:c5:00.4"), std::string::npos) << "@" << s.w;
    }
}

// One VM line is one screen row, whatever its length, and no row exceeds the
// width.
TEST(DetailPaneRender, EachLineOccupiesExactlyOneRow) {
    const Theme theme(ColorMode::Full, false);
    const auto lines = deviceDetailLines();
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderDetailPane(lines, theme), s);
        for (int y = 0; y < static_cast<int>(lines.size()); ++y) {
            EXPECT_FALSE(rowText(screen, y).empty()) << "@" << s.w << " row " << y;
        }
        // Nothing spilled past the last line into a wrapped continuation row.
        EXPECT_TRUE(rowText(screen, static_cast<int>(lines.size())).empty()) << "@" << s.w;
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w) << "@" << s.w << " row " << y;
        }
    }
}

// A value far wider than 80 columns is elided, visibly, on its own row.
TEST(DetailPaneRender, LongValueIsElidedAtEightyColumns) {
    const Theme theme(ColorMode::Full, false);
    const std::vector<std::string> lines{
        "Modalias: pci:v00001022d000015B8sv00001022sd000015B8bc0Csc03i30-and-then-some-more-so-it-"
        "definitely-does-not-fit-in-eighty-columns",
        "Status:   enabled",
    };
    ftxui::Screen screen = renderTo(views::renderDetailPane(lines, theme), {80, 24});
    const std::string first = rowText(screen, 0);
    EXPECT_LE(ftxui::string_width(first), 80);
    EXPECT_NE(first.find("Modalias:"), std::string::npos);
    EXPECT_NE(first.find("…"), std::string::npos) << first;
    EXPECT_EQ(first.find("eighty-columns"), std::string::npos) << first;
    // The line below it is untouched — the long value claimed no extra row.
    EXPECT_NE(rowText(screen, 1).find("Status:"), std::string::npos);
}

// Plain mode stays ASCII, ellipsis included.
TEST(DetailPaneRender, PlainModeElidesWithAsciiDots) {
    const Theme plain(ColorMode::Plain, false);
    const std::vector<std::string> lines{"Sysfs:    " + std::string(200, 'x')};
    ftxui::Screen screen = renderTo(views::renderDetailPane(lines, plain), {80, 24});
    const std::string row = rowText(screen, 0);
    EXPECT_NE(row.find("..."), std::string::npos);
    for (const unsigned char c : row) EXPECT_LT(c, 0x80U) << row;
}

TEST(DetailPaneRender, EmptyLineListRendersNothing) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderDetailPane({}, theme), {80, 24});
    for (int y = 0; y < screen.dimy(); ++y) EXPECT_TRUE(rowText(screen, y).empty()) << y;
}

}  // namespace
}  // namespace devmgr::tui
