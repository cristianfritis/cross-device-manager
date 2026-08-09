// Selection regression tests for the pass-2 bug-fix group (tasks 8.1-8.3).
//
// B1 (8.1): the selection marker, the selection highlight and the row the
// detail pane follows must be one and the same row. Pass 1 keyed the highlight
// off FTXUI's `focused_entry` (which the mouse moves independently of
// `selected`), so a mouse move split the marker, the bar and the detail pane
// across three different rows. The fix keys every selection signal off the
// selected index alone, which these tests pin at the render seam.
#include "tui/src/render_util.hpp"
#include "tui/src/selection.hpp"
#include "tui/src/views/devices_view.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>
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

bool rowHasInverted(const ftxui::Screen& screen, int y) {
    for (int x = 0; x < screen.dimx(); ++x) {
        if (screen.PixelAt(x, y).inverted) return true;
    }
    return false;
}

bool rowHasMarker(const ftxui::Screen& screen, int y) {
    return rowText(screen, y).substr(0, 2) == "> ";
}

// A three-row list with exactly one row selected, as the shell composes it.
ftxui::Element listWithSelection(int selectedRow, bool listFocused, const Theme& theme) {
    ftxui::Elements rows;
    const std::array<const char*, 3> labels{{"nvme0n1 enabled", "eth0 disabled", "wlan0 unknown"}};
    for (int i = 0; i < 3; ++i) {
        rows.push_back(render::menuRow(labels[static_cast<std::size_t>(i)], i == selectedRow,
                                       listFocused, std::nullopt, std::nullopt, theme));
    }
    return ftxui::vbox(std::move(rows));
}

// -------------------------------------------------------------------------
// 8.1 / B1 — one row carries every selection signal.
// -------------------------------------------------------------------------

TEST(SelectionInvariant, MarkerAndHighlightLandOnTheSameRow) {
    const Theme theme(ColorMode::Full, false);
    for (int selected = 0; selected < 3; ++selected) {
        ftxui::Screen screen = renderTo(listWithSelection(selected, true, theme), {40, 3});
        for (int y = 0; y < 3; ++y) {
            EXPECT_EQ(rowHasMarker(screen, y), y == selected) << "marker row " << y;
            EXPECT_EQ(rowHasInverted(screen, y), y == selected) << "highlight row " << y;
        }
    }
}

// The regression itself: pass 1 could highlight a row that was not the selected
// one (the mouse moved FTXUI's focused entry away from `selected`). A row that
// is not selected must show no marker and no highlight even while the list owns
// the keyboard.
TEST(SelectionInvariant, NonSelectedRowIsNeverHighlighted) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(render::menuRow("eth0 disabled", /*selected=*/false, /*listFocused=*/true,
                                 std::nullopt, std::nullopt, theme),
                 {40, 1});
    EXPECT_FALSE(rowHasMarker(screen, 0));
    EXPECT_FALSE(rowHasInverted(screen, 0));
}

// Losing keyboard focus to the filter must not move the cursor: the marker
// stays on the selected row (so it still matches the detail pane) and only the
// reverse video — the "this list has the keyboard" cue — is dropped.
TEST(SelectionInvariant, SelectionKeepsItsRowWhenTheListLosesFocus) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(listWithSelection(1, /*listFocused=*/false, theme), {40, 3});
    EXPECT_TRUE(rowHasMarker(screen, 1));
    EXPECT_FALSE(rowHasMarker(screen, 0));
    EXPECT_FALSE(rowHasMarker(screen, 2));
    for (int y = 0; y < 3; ++y) EXPECT_FALSE(rowHasInverted(screen, y)) << "row " << y;
}

// The Devices rows go through the same primitive, so the invariant holds there
// too — including with a status glyph and a state colour on the row.
TEST(SelectionInvariant, DeviceRowsShareTheInvariantAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen =
            renderTo(ftxui::vbox({
                         views::renderDeviceRow("nvme0n1 enabled", false, true,
                                                render::Glyph::Enabled, Role::Nominal, theme),
                         views::renderDeviceRow("eth0 disabled", true, true,
                                                render::Glyph::Disabled, Role::Muted, theme),
                     }),
                     s);
        EXPECT_FALSE(rowHasMarker(screen, 0)) << s.w;
        EXPECT_FALSE(rowHasInverted(screen, 0)) << s.w;
        EXPECT_TRUE(rowHasMarker(screen, 1)) << s.w;
        EXPECT_TRUE(rowHasInverted(screen, 1)) << s.w;
    }
}

// -------------------------------------------------------------------------
// 8.2 / B2 — one selection treatment, identical in all four views.
// -------------------------------------------------------------------------

// Attributes of the first styled cell of a row, which is what the eye reads as
// "the selection bar".
struct RowStyle {
    ftxui::Color fg;
    bool bold;
    bool inverted;
    bool operator==(const RowStyle&) const = default;
};

RowStyle styleOf(const ftxui::Screen& screen, int y) {
    const ftxui::Pixel& p = screen.PixelAt(0, y);
    return {p.foreground_color, p.bold, p.inverted};
}

// The selected row must look the same whatever state the row itself is in:
// pass 1 inverted the row's semantic colour, so the bar was green on an enabled
// device, red on a disabled one and different again per view.
TEST(SelectionTreatment, IsIdenticalWhateverTheRowState) {
    const Theme theme(ColorMode::Full, false);
    const std::array<std::optional<Role>, 8> rowRoles{{std::nullopt, Role::Nominal, Role::Success,
                                                       Role::Danger, Role::Warning, Role::Info,
                                                       Role::Muted, Role::Accent}};
    const RowStyle expected{ftxui::Color::Cyan, true, true};
    for (const auto& role : rowRoles) {
        ftxui::Screen screen =
            renderTo(render::menuRow("row text", /*selected=*/true, /*listFocused=*/true,
                                     std::nullopt, role, theme),
                     {40, 1});
        EXPECT_EQ(styleOf(screen, 0), expected) << "role " << (role ? static_cast<int>(*role) : -1);
    }
}

// Same treatment across the four view row builders (Devices goes through
// renderDeviceRow, the other three through menuRow directly), each carrying the
// state its own view actually colours.
TEST(SelectionTreatment, IsIdenticalAcrossAllFourViews) {
    const Theme theme(ColorMode::Full, false);
    const RowStyle expected{ftxui::Color::Cyan, true, true};
    EXPECT_EQ(styleOf(renderTo(views::renderDeviceRow("eth0 disabled", true, true,
                                                      render::Glyph::Disabled, Role::Muted, theme),
                               {40, 1}),
                      0),
              expected)
        << "devices";
    const std::array<std::optional<Role>, 3> otherViews{{Role::Nominal, Role::Info, Role::Accent}};
    const std::array<const char*, 3> labels{
        {"i915 signed: yes", "Dock fw 1.1 available", "a1b2c3 boot healthy"}};
    for (std::size_t i = 0; i < otherViews.size(); ++i) {
        ftxui::Screen screen = renderTo(
            render::menuRow(labels[i], true, true, std::nullopt, otherViews[i], theme), {40, 1});
        EXPECT_EQ(styleOf(screen, 0), expected) << "view " << i;
    }
}

// Colour independence: with colour degraded the treatment is still one and the
// same — reverse video plus the "> " marker, no hue anywhere (§10).
TEST(SelectionTreatment, DegradesToOneMonoTreatment) {
    const Theme mono(ColorMode::Mono, false);
    const std::array<std::optional<Role>, 4> rowRoles{
        {Role::Nominal, Role::Success, Role::Danger, Role::Muted}};
    for (const auto& role : rowRoles) {
        ftxui::Screen screen =
            renderTo(render::menuRow("row text", true, true, std::nullopt, role, mono), {40, 1});
        EXPECT_EQ(styleOf(screen, 0), (RowStyle{ftxui::Color::Default, true, true}));
        EXPECT_TRUE(rowHasMarker(screen, 0));
    }
}

// Unselected rows keep their own semantic colour — the selection treatment
// replaces the state colour only on the row that is actually selected.
TEST(SelectionTreatment, UnselectedRowsKeepTheirStateColour) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(render::menuRow("nvme0n1 error", /*selected=*/false, /*listFocused=*/true,
                                 std::nullopt, Role::Danger, theme),
                 {40, 1});
    EXPECT_EQ(styleOf(screen, 0).fg, ftxui::Color::Red);
}

// A nominal row's dim attribute must not follow it into the selection: the
// selected row takes the accent treatment whole, so the one row the user is
// reading is never the quietest thing on screen. (design Risks: "does dim
// survive inverted video".)
TEST(SelectionTreatment, SelectedNominalRowIsNotDim) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen selected =
        renderTo(views::renderDeviceRow("nvme0n1 enabled", /*selected=*/true, /*listFocused=*/true,
                                        render::Glyph::Enabled, Role::Nominal, theme),
                 {40, 1});
    const ftxui::Pixel& bar = selected.PixelAt(0, 0);
    EXPECT_EQ(bar.foreground_color, ftxui::Color::Cyan);
    EXPECT_TRUE(bar.inverted);
    EXPECT_FALSE(bar.dim);

    // Unselected, the same row keeps the quiet paint.
    ftxui::Screen resting =
        renderTo(views::renderDeviceRow("nvme0n1 enabled", /*selected=*/false, /*listFocused=*/true,
                                        render::Glyph::Enabled, Role::Nominal, theme),
                 {40, 1});
    EXPECT_EQ(resting.PixelAt(2, 0).foreground_color, ftxui::Color::Green);
    EXPECT_TRUE(resting.PixelAt(2, 0).dim);
}

// -------------------------------------------------------------------------
// 8.3 / B3 — headers and placeholders are not selectable; an empty list has no
// cursor at all.
// -------------------------------------------------------------------------

// A Devices-shaped list: bus group headers at rows 0 and 3, devices elsewhere.
const nav::Selectable kGroupedList = [](int row) { return row != 0 && row != 3; };
// The empty list: a single "(no devices)" placeholder and nothing else.
const nav::Selectable kNothingSelectable = [](int) { return false; };

TEST(SnapSelection, LeavesADataRowWhereItIs) {
    for (int row : {1, 2, 4}) {
        EXPECT_EQ(nav::snapToSelectable(row, 5, 1, kGroupedList), row);
        EXPECT_EQ(nav::snapToSelectable(row, 5, -1, kGroupedList), row);
    }
}

TEST(SnapSelection, SkipsAHeaderInTheDirectionOfTravel) {
    // Arrowing down onto the header at row 3 continues down to row 4.
    EXPECT_EQ(nav::snapToSelectable(3, 5, 1, kGroupedList), 4);
    // Arrowing up onto it continues up to row 2.
    EXPECT_EQ(nav::snapToSelectable(3, 5, -1, kGroupedList), 2);
}

TEST(SnapSelection, FallsBackWhenTheDirectionOfTravelRunsOut) {
    // The first row is a header and there is nothing above it: sweep back down.
    EXPECT_EQ(nav::snapToSelectable(0, 5, -1, kGroupedList), 1);
    // A trailing header with nothing below: sweep back up.
    const nav::Selectable trailingHeader = [](int row) { return row < 2; };
    EXPECT_EQ(nav::snapToSelectable(2, 3, 1, trailingHeader), 1);
}

TEST(SnapSelection, ClampsAnOutOfRangeIndex) {
    EXPECT_EQ(nav::snapToSelectable(99, 5, 1, kGroupedList), 4);
    EXPECT_EQ(nav::snapToSelectable(-7, 5, 1, kGroupedList), 1);
    EXPECT_EQ(nav::snapToSelectable(3, 0, 1, kGroupedList), 0);  // no rows at all
}

TEST(SnapSelection, EmptyListKeepsItsIndexAndReportsNoSelectableRow) {
    EXPECT_EQ(nav::snapToSelectable(0, 1, 1, kNothingSelectable), 0);
    EXPECT_FALSE(nav::anySelectable(1, kNothingSelectable));
    EXPECT_TRUE(nav::anySelectable(5, kGroupedList));
}

// The rendered consequence: when the only row is a placeholder, the shell marks
// it unselected, so an empty list shows no marker and no highlight — no cursor
// parked on a row where every verb would refuse.
TEST(EmptyListRender, ShowsNoCursorOnThePlaceholder) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen =
            renderTo(views::renderDeviceRow("(no devices)", /*selected=*/false,
                                            /*listFocused=*/true, std::nullopt, Role::Muted, theme),
                     s);
        EXPECT_FALSE(rowHasMarker(screen, 0)) << s.w;
        EXPECT_FALSE(rowHasInverted(screen, 0)) << s.w;
        EXPECT_NE(rowText(screen, 0).find("(no devices)"), std::string::npos) << s.w;
    }
}

// A group header keeps its muted styling and never takes the cursor, even while
// the list owns the keyboard.
TEST(EmptyListRender, GroupHeaderNeverTakesTheCursor) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(ftxui::vbox({
                     views::renderDeviceRow("PCI", false, true, std::nullopt, Role::Muted, theme),
                     views::renderDeviceRow("nvme0n1 enabled", true, true, render::Glyph::Enabled,
                                            Role::Nominal, theme),
                 }),
                 {40, 2});
    EXPECT_FALSE(rowHasMarker(screen, 0));
    EXPECT_FALSE(rowHasInverted(screen, 0));
    EXPECT_TRUE(rowHasMarker(screen, 1));
    EXPECT_TRUE(rowHasInverted(screen, 1));
}

// -------------------------------------------------------------------------
// 10.2 / R3 — bounded reveal. Pure offset maths, so every point of the reveal
// is renderable off-screen; the tick counter that drives it lives in the shell.
// -------------------------------------------------------------------------

constexpr int kLeadInTicks = 4;  // render_util's lead-in, mirrored for clarity

TEST(BoundedReveal, ShortNameNeverReveals) {
    EXPECT_EQ(render::revealMaxOffset("eth0", 20), 0);
    EXPECT_EQ(render::revealWindow("eth0", 20, 0), "eth0");
    EXPECT_EQ(render::revealWindow("eth0", 20, 99), "eth0");  // offset is irrelevant
    EXPECT_EQ(render::revealOffset(1000, 0), 0);              // nothing to advance to
}

TEST(BoundedReveal, RevealsTheElidedTailAndComesToRest) {
    const std::string name = "Realtek Semiconductor RTL8111 Gigabit Ethernet";
    constexpr int kWidth = 20;
    const int maxOffset = render::revealMaxOffset(name, kWidth);
    ASSERT_GT(maxOffset, 0);

    // At rest at the start: the head of the name.
    EXPECT_EQ(render::revealWindow(name, kWidth, 0), name.substr(0, kWidth));
    // At rest at the end: the tail is visible.
    EXPECT_EQ(render::revealWindow(name, kWidth, maxOffset), name.substr(name.size() - kWidth));

    // The offset holds through the lead-in, advances one glyph per tick, and
    // then STOPS — it never wraps back to the beginning the way pass 1 did.
    EXPECT_EQ(render::revealOffset(0, maxOffset), 0);
    EXPECT_EQ(render::revealOffset(kLeadInTicks, maxOffset), 0);
    EXPECT_EQ(render::revealOffset(kLeadInTicks + 1, maxOffset), 1);
    EXPECT_EQ(render::revealOffset(kLeadInTicks + maxOffset, maxOffset), maxOffset);
    EXPECT_EQ(render::revealOffset(kLeadInTicks + maxOffset + 500, maxOffset), maxOffset);
}

TEST(BoundedReveal, OffsetIsMonotonicSoTheTickerCanStop) {
    constexpr int kMax = 12;
    int previous = -1;
    for (int tick = 0; tick < 100; ++tick) {
        const int offset = render::revealOffset(tick, kMax);
        EXPECT_GE(offset, previous);
        EXPECT_LE(offset, kMax);
        previous = offset;
    }
    EXPECT_EQ(previous, kMax);  // reached rest and stayed there
}

// Multi-byte names must not split mid-codepoint at any offset.
TEST(BoundedReveal, NeverSplitsAMultiByteGlyph) {
    const std::string name = "Logitech StreamCam™ — 1080p Webcam";
    constexpr int kWidth = 12;
    const int maxOffset = render::revealMaxOffset(name, kWidth);
    for (int offset = 0; offset <= maxOffset; ++offset) {
        const std::string window = render::revealWindow(name, kWidth, offset);
        EXPECT_EQ(ftxui::string_width(window), kWidth) << "offset " << offset;
        // A byte sequence that split a codepoint would begin with a UTF-8
        // continuation byte.
        ASSERT_FALSE(window.empty());
        EXPECT_NE(static_cast<unsigned char>(window.front()) & 0xC0U, 0x80U) << "offset " << offset;
    }
}

// The structural bound design.md asks for: at every offset, including the two
// resting ones, the rendered row stays inside its region and writes nothing out
// of bounds.
TEST(BoundedReveal, RowStaysWithinItsRegionAtEveryOffset) {
    const Theme theme(ColorMode::Full, false);
    const std::string name = "Advanced Micro Devices Family 19h Root Complex Bridge";
    for (Size s : kSizes) {
        const int rowWidth = s.w - 6;
        const int maxOffset = render::revealMaxOffset(name, rowWidth);
        for (const int offset : {0, maxOffset / 2, maxOffset}) {
            ftxui::Screen screen =
                renderTo(render::menuRow(render::revealWindow(name, rowWidth, offset), true, true,
                                         std::nullopt, std::nullopt, theme),
                         s);
            EXPECT_LE(ftxui::string_width(rowText(screen, 0)), s.w)
                << "@" << s.w << " offset " << offset;
            for (int y = 1; y < screen.dimy(); ++y) {
                EXPECT_TRUE(rowText(screen, y).empty()) << "@" << s.w << " row " << y;
            }
        }
    }
}

// Non-selected rows do not reveal; they elide right, visibly.
TEST(BoundedReveal, NonSelectedRowsElideRight) {
    const Theme theme(ColorMode::Full, false);
    const std::string name = "Advanced Micro Devices Family 19h Root Complex Bridge";
    ftxui::Screen screen = renderTo(render::menuRow(name, /*selected=*/false, /*listFocused=*/true,
                                                    std::nullopt, std::nullopt, theme),
                                    {24, 1});
    const std::string row = rowText(screen, 0);
    EXPECT_EQ(ftxui::string_width(row), 24);
    EXPECT_NE(row.find("…"), std::string::npos);
    EXPECT_EQ(row.find("Bridge"), std::string::npos);  // the tail is not shown
}

// -------------------------------------------------------------------------
// 10.3 / R4 — criticality badge: on the name, never on the state glyph, never
// danger, and legible with no colour at all.
// -------------------------------------------------------------------------

// Cell attributes at a given column, for asserting that one signal does not
// bleed into its neighbour.
ftxui::Color fgAt(const ftxui::Screen& screen, int x) {
    return screen.PixelAt(x, 0).foreground_color;
}

TEST(CriticalityBadge, MarksTheRowWithoutTouchingTheStateGlyph) {
    const Theme theme(ColorMode::Full, false);
    const render::Badge essential{render::Glyph::Essential, Role::Warning};
    ftxui::Screen screen =
        renderTo(views::renderDeviceRow("nvme0n1 enabled", false, false, render::Glyph::Enabled,
                                        Role::Nominal, theme, essential),
                 {40, 1});
    const std::string row = rowText(screen, 0);
    // "  " prefix, then the state glyph, then the badge, then the label.
    ASSERT_EQ(row.substr(0, 5), "  + #");
    EXPECT_EQ(fgAt(screen, 2), ftxui::Color::Green);   // the "+" keeps nominal
    EXPECT_EQ(fgAt(screen, 4), ftxui::Color::Yellow);  // the badge is warning
    EXPECT_EQ(fgAt(screen, 6), ftxui::Color::Green);   // the label keeps its state colour
    // The badge is louder than the row it marks: warning is not dimmed by the
    // nominal row around it.
    EXPECT_TRUE(screen.PixelAt(2, 0).dim);
    EXPECT_FALSE(screen.PixelAt(4, 0).dim);
}

TEST(CriticalityBadge, NeverUsesTheDangerRole) {
    const Theme theme(ColorMode::Full, false);
    for (const render::Glyph g : {render::Glyph::Essential, render::Glyph::Important}) {
        ftxui::Screen screen =
            renderTo(render::menuRow("mod", false, false, std::nullopt, std::nullopt, theme,
                                     render::Badge{g, Role::Warning}),
                     {40, 1});
        // The badge column is warning; nothing on the row is red.
        for (int x = 0; x < screen.dimx(); ++x) {
            EXPECT_NE(fgAt(screen, x), ftxui::Color::Red);
        }
    }
}

// A module that is BOTH essential and unsigned shows both signals, in different
// columns, without either overwriting the other.
TEST(CriticalityBadge, CoexistsWithAnUnsignedRow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(render::menuRow("nvidia   signed: NO", false, false, std::nullopt, Role::Danger,
                                 theme, render::Badge{render::Glyph::Essential, Role::Warning}),
                 {40, 1});
    bool anyWarning = false;
    bool anyDanger = false;
    for (int x = 0; x < screen.dimx(); ++x) {
        if (fgAt(screen, x) == ftxui::Color::Yellow) anyWarning = true;
        if (fgAt(screen, x) == ftxui::Color::Red) anyDanger = true;
    }
    EXPECT_TRUE(anyWarning);  // criticality on the badge
    EXPECT_TRUE(anyDanger);   // unsigned on the label
}

// The two levels are told apart with no colour, and both are ASCII in every mode
// (the Unicode warning signs are ambiguous-width and would reflow the row).
TEST(CriticalityBadge, LevelsAreDistinctAsciiGlyphsInEveryMode) {
    for (const ColorMode mode : {ColorMode::Full, ColorMode::Mono, ColorMode::Plain}) {
        for (const bool unicodeOptIn : {false, true}) {
            const Theme theme(mode, unicodeOptIn);
            const std::string essential = rowText(
                renderTo(render::menuRow("mod", false, false, std::nullopt, std::nullopt, theme,
                                         render::Badge{render::Glyph::Essential, Role::Warning}),
                         {40, 1}),
                0);
            const std::string important = rowText(
                renderTo(render::menuRow("mod", false, false, std::nullopt, std::nullopt, theme,
                                         render::Badge{render::Glyph::Important, Role::Warning}),
                         {40, 1}),
                0);
            EXPECT_NE(essential.find('#'), std::string::npos);
            EXPECT_NE(important.find('~'), std::string::npos);
            EXPECT_NE(essential, important);
            for (const unsigned char c : essential) EXPECT_LT(c, 0x80U) << essential;
        }
    }
}

// An ordinary row is unmarked: the marker means something because most rows do
// not carry it.
TEST(CriticalityBadge, OrdinaryRowsAreUnmarked) {
    const Theme theme(ColorMode::Mono, false);
    const std::string row = rowText(
        renderTo(render::menuRow("i915", false, false, std::nullopt, std::nullopt, theme), {40, 1}),
        0);
    EXPECT_EQ(row.find('#'), std::string::npos);
    EXPECT_EQ(row.find('~'), std::string::npos);
}

// The badge keeps its own colour on the selected row too — criticality is a
// property of the component, not of where the cursor happens to be.
TEST(CriticalityBadge, SurvivesTheSelectionTreatment) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen =
        renderTo(render::menuRow("amdgpu", true, true, std::nullopt, Role::Nominal, theme,
                                 render::Badge{render::Glyph::Essential, Role::Warning}),
                 {40, 1});
    EXPECT_TRUE(rowHasMarker(screen, 0));
    EXPECT_TRUE(rowHasInverted(screen, 0));
    EXPECT_NE(rowText(screen, 0).find('#'), std::string::npos);
    bool anyWarning = false;
    for (int x = 0; x < screen.dimx(); ++x) {
        if (fgAt(screen, x) == ftxui::Color::Yellow) anyWarning = true;
    }
    EXPECT_TRUE(anyWarning);
}

}  // namespace
}  // namespace devmgr::tui
