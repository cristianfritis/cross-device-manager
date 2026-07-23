// States-matrix and colour-independence render tests (docs/DESIGN.md §12.1,
// tui-instrument-panel-polish group 5). Three proofs layered on the per-view
// render functions and the two colour-bearing primitives (render::menuRow,
// renderStatusBar):
//   5.1  Every view composes across the six UI states (empty, loading, prompt,
//        confirmation, refusal, failure) at all three reference sizes with no
//        row exceeding the screen width.
//   5.2  In mono mode every represented state is still identifiable by its glyph
//        and/or state text with NO colour applied; in plain mode a view emits
//        ASCII only (borders and separators degrade to ASCII).
//   5.3  In full mode each semantic role decorates its element with the expected
//        16-colour ANSI attribute, and the per-view status/banner roles wire
//        that colour through to the composed frame.
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>  // string_width

#include <gtest/gtest.h>

#include "tui/src/render_util.hpp"
#include "tui/src/theme.hpp"
#include "tui/src/views/devices_view.hpp"
#include "tui/src/views/modules_view.hpp"
#include "tui/src/views/snapshots_view.hpp"
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/updates_view.hpp"

namespace devmgr::tui {
namespace {

using ftxui::Color;

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

bool screenContains(const ftxui::Screen& screen, const std::string& needle) {
    for (int y = 0; y < screen.dimy(); ++y) {
        if (rowText(screen, y).find(needle) != std::string::npos) return true;
    }
    return false;
}

// True iff no cell exceeds the screen width in display columns (no out-of-bounds
// writes). Box-drawing borders are multi-byte UTF-8, so width is measured in
// columns, not bytes.
bool noRowOverflows(const ftxui::Screen& screen, int width) {
    for (int y = 0; y < screen.dimy(); ++y) {
        if (ftxui::string_width(rowText(screen, y)) > width) return false;
    }
    return true;
}

// True iff any cell carries colour, counting the dim attribute (muted's signal).
// `inverted`/`bold` are not colour (they are the selection/focus/emphasis
// signals), so a mono screen with reverse-video status bar and bold selection
// still reports false. Used on primitive renders (rows, status bar) that carry
// no scroll affordance.
bool screenHasColour(const ftxui::Screen& screen) {
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            const ftxui::Pixel& p = screen.PixelAt(x, y);
            if (p.foreground_color != Color::Default || p.background_color != Color::Default ||
                p.dim) {
                return true;
            }
        }
    }
    return false;
}

// True iff any cell carries a foreground/background hue. Ignores dim, so it is
// robust to FTXUI's scroll indicator (which dims its gutter regardless of
// theme) — the whole-view mono proof needs the hue check; muted's dim-drop in
// mono is proven separately on the vscroll-free row primitives.
bool screenHasHue(const ftxui::Screen& screen) {
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            const ftxui::Pixel& p = screen.PixelAt(x, y);
            if (p.foreground_color != Color::Default || p.background_color != Color::Default) {
                return true;
            }
        }
    }
    return false;
}

// True iff any light box-drawing character is present. Full-mode borders and
// separators use these (┌ ─ │); plain-mode degrades them to '+'/'-' ASCII. The
// heavy scroll-indicator glyph (┃) and block (█) are deliberately not in this
// set, so this proves border degradation independent of the scroll affordance.
bool hasBoxDrawing(const ftxui::Screen& screen) {
    return screenContains(screen, "┌") ||  // ┌ top-left corner
           screenContains(screen, "─") ||  // ─ horizontal
           screenContains(screen, "│");    // │ vertical
}

// The pixel attribute a role produces in full mode: a hue for the coloured
// roles, the dim attribute for muted (docs/DESIGN.md §4.1, mirrored from
// test_theme.cpp).
struct RoleColour {
    Role role;
    Color fg;
    bool dim;
};
const std::array<RoleColour, 6> kRoleColours{{
    {Role::Accent, Color::Cyan, false},
    {Role::Success, Color::Green, false},
    {Role::Warning, Color::Yellow, false},
    {Role::Danger, Color::Red, false},
    {Role::Info, Color::Blue, false},
    {Role::Muted, Color::Default, true},
}};

// True iff any cell matches (foreground, dim), optionally also reverse-video.
bool hasRolePixel(const ftxui::Screen& screen, Color fg, bool dim, bool inverted = false) {
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            const ftxui::Pixel& p = screen.PixelAt(x, y);
            if (p.foreground_color == fg && p.dim == dim && (!inverted || p.inverted)) return true;
        }
    }
    return false;
}

// -------------------------------------------------------------------------
// Sample bodies. Content is ASCII-only so the plain-mode ASCII assertion is a
// clean proof about borders/separators, not about incidental Unicode in text.
// The list rows mirror the shell's colouring seam: Devices carry a glyph AND a
// role (glyph is the mono signal); the colour-only lists carry the state word
// in the row text (that word is the mono signal).
// -------------------------------------------------------------------------

ftxui::Element deviceRows(const Theme& theme) {
    using namespace ftxui;
    return vbox({
        views::renderDeviceRow("eth0", true, true, render::Glyph::Disabled, Role::Danger, theme),
        views::renderDeviceRow("nvme0n1", false, false, render::Glyph::Enabled, Role::Success,
                               theme),
        views::renderDeviceRow("wlan0", false, false, render::Glyph::Unavailable, Role::Warning,
                               theme),
    });
}

ftxui::Element moduleRows(const Theme& theme) {
    return ftxui::vbox({
        render::menuRow("nvidia   signed: NO", true, true, std::nullopt, Role::Danger, theme),
        render::menuRow("i915     signed: yes", false, false, std::nullopt, Role::Success, theme),
        render::menuRow("acpi     signed: ?", false, false, std::nullopt, Role::Muted, theme),
    });
}

ftxui::Element updateRows(const Theme& theme) {
    return ftxui::vbox({
        render::menuRow("Dock fw   1.1 available", true, true, std::nullopt, Role::Info, theme),
        render::menuRow("SSD fw    up to date", false, false, std::nullopt, Role::Muted, theme),
        render::menuRow("BIOS      error: fetch", false, false, std::nullopt, Role::Danger, theme),
    });
}

ftxui::Element snapshotRows(const Theme& theme) {
    return ftxui::vbox({
        render::menuRow("a1b2c3 boot   healthy", true, true, render::Glyph::Marker, Role::Accent,
                        theme),
        render::menuRow("d4e5f6 manual corrupt", false, false, std::nullopt, Role::Danger, theme),
        render::menuRow("99aa88 auto   unsupported", false, false, std::nullopt, Role::Warning,
                        theme),
    });
}

// A single UI state expressed at the view seam: the collection body, the status
// message, its outcome role (nullopt = neutral prompt/steady), and a marker
// string that must survive to the screen.
struct StateCase {
    const char* name;
    std::function<ftxui::Element(const Theme&)> body;
    std::string status;
    std::optional<Role> role;
    std::string marker;
};

// The six states for a master-detail list view (Devices/Modules/Updates share
// this shape). `rows` supplies the populated-list body used by the interactive
// states. Prompt/confirmation are neutral (a modal is an interactive prompt,
// not an outcome, §group 4); refusal/failure carry the danger outcome.
std::vector<StateCase> listStates(const std::function<ftxui::Element(const Theme&)>& rows) {
    return {
        {"empty", [](const Theme&) { return ftxui::text("(no items)"); }, "0 items", std::nullopt,
         "(no items)"},
        {"loading", [](const Theme&) { return ftxui::text("Refreshing"); }, "Refreshing",
         std::nullopt, "Refreshing"},
        {"prompt", rows, "filter: eth", std::nullopt, "filter: eth"},
        {"confirmation", rows, "disable eth0? (y/n)", std::nullopt, "(y/n)"},
        {"refusal", rows, "Refused: Secure Boot would reject this", Role::Danger, "Refused:"},
        {"failure", rows, "Error: operation failed", Role::Danger, "Error:"},
    };
}

ftxui::Element buildDevices(const StateCase& s, const Theme& theme) {
    return views::renderDevicesView({.activeTab = 0,
                                     .filterInput = ftxui::text("filter devices"),
                                     .deviceList = s.body(theme),
                                     .detail = ftxui::text("Name: eth0  Status: disabled"),
                                     .statusText = s.status,
                                     .leftPaneWidth = 44,
                                     .statusRole = s.role},
                                    theme);
}

ftxui::Element buildModules(const StateCase& s, const Theme& theme) {
    return views::renderModulesView({.activeTab = 1,
                                     .banner = "Secure Boot: enabled",
                                     .filterInput = ftxui::text("filter modules"),
                                     .list = s.body(theme),
                                     .detail = ftxui::text("Module: nvidia  signed: NO"),
                                     .statusText = s.status,
                                     .leftPaneWidth = 72,
                                     .bannerRole = Role::Info,
                                     .statusRole = s.role},
                                    theme);
}

ftxui::Element buildUpdates(const StateCase& s, const Theme& theme) {
    return views::renderUpdatesView({.activeTab = 2,
                                     .banner = "fwupd 2.0.1 - 3 updates",
                                     .requestBanner = "",
                                     .list = s.body(theme),
                                     .detail = ftxui::text("Release notes"),
                                     .statusText = s.status,
                                     .leftPaneWidth = 72,
                                     .statusRole = s.role},
                                    theme);
}

// Snapshots differ: confirmation is the restore-preview modal and failure adds
// the recovery-guidance panel, so it gets a bespoke state list.
std::vector<StateCase> snapshotStates() {
    return {
        {"empty", [](const Theme&) { return ftxui::text("(no snapshots)"); }, "0 snapshots",
         std::nullopt, "(no snapshots)"},
        {"loading", [](const Theme&) { return ftxui::text("Loading snapshots"); }, "Loading",
         std::nullopt, "Loading snapshots"},
        {"prompt", snapshotRows, "filter: boot", std::nullopt, "filter: boot"},
        {"confirmation", snapshotRows, "restore this snapshot? (y/n)", std::nullopt, "(y/n)"},
        {"refusal", snapshotRows, "Refused: snapshot corrupt", Role::Danger, "Refused:"},
        {"failure", snapshotRows, "Error: restore incomplete", Role::Danger, "Error:"},
    };
}

views::SnapshotsView snapshotView(const StateCase& s, const Theme& theme) {
    views::SnapshotsView v{.activeTab = 3,
                           .banner = "12 snapshots - HEAD a1b2c3",
                           .statusText = s.status,
                           .leftPaneWidth = 72,
                           .statusRole = s.role};
    if (std::string(s.name) == "confirmation") {
        v.showPreview = true;
        v.previewLines = {"restore preview", "current -> a1b2c3", "converges cleanly"};
    } else {
        v.filterInput = ftxui::text("filter snapshots");
        v.list = s.body(theme);
        v.detail = ftxui::text("Snapshot a1b2c3");
        if (std::string(s.name) == "failure") {
            v.guidanceLines = {"Restore did not fully converge.", "safety snapshot: f0e1d2"};
        }
    }
    return v;
}

// =========================================================================
// 5.1 States matrix — every view composes across every state at every size.
// =========================================================================

TEST(StatesMatrix, DevicesComposesEveryState) {
    const Theme theme(ColorMode::Full, false);
    for (const auto& s : listStates(deviceRows)) {
        for (Size sz : kSizes) {
            ftxui::Screen screen = renderTo(buildDevices(s, theme), sz);
            EXPECT_TRUE(screenContains(screen, "Devices")) << s.name << " @" << sz.w;
            EXPECT_TRUE(screenContains(screen, s.marker)) << s.name << " @" << sz.w;
            EXPECT_TRUE(noRowOverflows(screen, sz.w)) << s.name << " @" << sz.w;
        }
    }
}

TEST(StatesMatrix, ModulesComposesEveryState) {
    const Theme theme(ColorMode::Full, false);
    for (const auto& s : listStates(moduleRows)) {
        for (Size sz : kSizes) {
            ftxui::Screen screen = renderTo(buildModules(s, theme), sz);
            EXPECT_TRUE(screenContains(screen, "Modules")) << s.name << " @" << sz.w;
            EXPECT_TRUE(screenContains(screen, s.marker)) << s.name << " @" << sz.w;
            EXPECT_TRUE(noRowOverflows(screen, sz.w)) << s.name << " @" << sz.w;
        }
    }
}

TEST(StatesMatrix, UpdatesComposesEveryState) {
    const Theme theme(ColorMode::Full, false);
    for (const auto& s : listStates(updateRows)) {
        for (Size sz : kSizes) {
            ftxui::Screen screen = renderTo(buildUpdates(s, theme), sz);
            EXPECT_TRUE(screenContains(screen, "Updates")) << s.name << " @" << sz.w;
            EXPECT_TRUE(screenContains(screen, s.marker)) << s.name << " @" << sz.w;
            EXPECT_TRUE(noRowOverflows(screen, sz.w)) << s.name << " @" << sz.w;
        }
    }
}

TEST(StatesMatrix, SnapshotsComposesEveryState) {
    const Theme theme(ColorMode::Full, false);
    for (const auto& s : snapshotStates()) {
        for (Size sz : kSizes) {
            ftxui::Screen screen =
                renderTo(views::renderSnapshotsView(snapshotView(s, theme), theme), sz);
            EXPECT_TRUE(screenContains(screen, "Snapshots")) << s.name << " @" << sz.w;
            EXPECT_TRUE(screenContains(screen, s.marker)) << s.name << " @" << sz.w;
            EXPECT_TRUE(noRowOverflows(screen, sz.w)) << s.name << " @" << sz.w;
        }
    }
}

// =========================================================================
// 5.2 Colour independence — mono keeps glyph+text and drops colour; plain is
// ASCII-only.
// =========================================================================

// Devices are the glyph-bearing view: every device status stays identifiable by
// its ASCII glyph and its state word, with no colour, in mono mode.
TEST(ColourIndependence, DeviceGlyphAndTextSurviveMono) {
    const Theme mono(ColorMode::Mono, false);
    const std::array<std::tuple<render::Glyph, std::string, std::string>, 4> cases{{
        {render::Glyph::Enabled, "+", "eth0 enabled"},
        {render::Glyph::Disabled, "-", "wlan0 disabled"},
        {render::Glyph::Unavailable, "?", "usb0 unavailable"},
        {render::Glyph::Unsigned, "!", "modx unsigned"},
    }};
    for (const auto& [glyph, glyphStr, label] : cases) {
        // A role is supplied but must have no visible effect in mono.
        ftxui::Screen screen =
            renderTo(render::menuRow(label, false, false, glyph, Role::Danger, mono), {40, 1});
        const std::string row = rowText(screen, 0);
        EXPECT_NE(row.find(glyphStr), std::string::npos) << label;  // glyph present
        EXPECT_NE(row.find(label), std::string::npos) << label;     // state text present
        EXPECT_FALSE(screenHasColour(screen)) << label;             // colour dropped
    }
}

// The colour-only lists (Modules/Updates/Snapshots) carry the state word in the
// row text; mono keeps that text and drops the colour.
TEST(ColourIndependence, ColourOnlyRowsKeepStateTextInMono) {
    const Theme mono(ColorMode::Mono, false);
    for (auto rows : {&moduleRows, &updateRows, &snapshotRows}) {
        ftxui::Screen screen = renderTo((*rows)(mono), {60, 3});
        EXPECT_FALSE(screenHasColour(screen));
    }
    EXPECT_TRUE(screenContains(renderTo(moduleRows(mono), {60, 3}), "signed: NO"));
    EXPECT_TRUE(screenContains(renderTo(updateRows(mono), {60, 3}), "available"));
    EXPECT_TRUE(screenContains(renderTo(snapshotRows(mono), {60, 3}), "corrupt"));
}

// Status-line outcomes stay legible with no colour under mono (the reverse
// video remains, but reverse video is not colour).
TEST(ColourIndependence, StatusOutcomeTextSurvivesMono) {
    const Theme mono(ColorMode::Mono, false);
    const std::array<std::tuple<std::string, std::optional<Role>>, 4> cases{{
        {"Enabled eth0", Role::Success},
        {"Refused: Secure Boot would reject this", Role::Danger},
        {"Error: enable failed", Role::Danger},
        {"disable eth0? (y/n)", std::nullopt},
    }};
    for (const auto& [text, role] : cases) {
        ftxui::Screen screen = renderTo(views::renderStatusBar(text, role, mono), {80, 1});
        EXPECT_TRUE(screenContains(screen, text)) << text;
        EXPECT_FALSE(screenHasColour(screen)) << text;
    }
}

// Whole-view mono proof: a fully-populated view (danger outcome + coloured
// rows) renders with no hue anywhere once colour is degraded.
TEST(ColourIndependence, FullViewsHaveNoHueInMono) {
    const Theme mono(ColorMode::Mono, false);
    const StateCase refusal = listStates(deviceRows).at(4);  // danger outcome + coloured rows
    EXPECT_FALSE(screenHasHue(renderTo(buildDevices(refusal, mono), {120, 32}))) << "devices";
    EXPECT_FALSE(screenHasHue(renderTo(buildModules(refusal, mono), {120, 32}))) << "modules";
    EXPECT_FALSE(screenHasHue(renderTo(buildUpdates(refusal, mono), {120, 32}))) << "updates";
    const StateCase snapFail = snapshotStates().at(5);
    EXPECT_FALSE(screenHasHue(
        renderTo(views::renderSnapshotsView(snapshotView(snapFail, mono), mono), {120, 32})))
        << "snapshots";
}

// Plain mode: borders and separators degrade from box-drawing to '+'/'-' ASCII.
// Proven per view by the box-drawing set being present in full mode and absent
// in plain (the scroll affordance's heavy glyphs are excluded from that set).
TEST(ColourIndependence, BordersDegradeToAsciiInPlain) {
    const Theme full(ColorMode::Full, false);
    const Theme plain(ColorMode::Plain, false);
    const StateCase prompt = listStates(deviceRows).at(2);
    EXPECT_TRUE(hasBoxDrawing(renderTo(buildDevices(prompt, full), {120, 32}))) << "devices full";
    EXPECT_FALSE(hasBoxDrawing(renderTo(buildDevices(prompt, plain), {120, 32})))
        << "devices plain";
    EXPECT_FALSE(hasBoxDrawing(renderTo(buildModules(prompt, plain), {120, 32})))
        << "modules plain";
    EXPECT_FALSE(hasBoxDrawing(renderTo(buildUpdates(prompt, plain), {120, 32})))
        << "updates plain";
    const StateCase snapPrompt = snapshotStates().at(2);
    EXPECT_FALSE(hasBoxDrawing(
        renderTo(views::renderSnapshotsView(snapshotView(snapPrompt, plain), plain), {120, 32})))
        << "snapshots plain";
    // The restore-preview modal also frames with a border → ASCII in plain.
    const StateCase snapConfirm = snapshotStates().at(3);
    EXPECT_FALSE(hasBoxDrawing(
        renderTo(views::renderSnapshotsView(snapshotView(snapConfirm, plain), plain), {120, 32})))
        << "snapshots preview plain";
}

// =========================================================================
// 5.3 Role decorators — full mode maps each role to its ANSI attribute, and the
// per-view status/banner roles wire that colour into the frame.
// =========================================================================

// Every list view colours its rows through render::menuRow, so proving the role
// -> attribute mapping there covers all four lists at once.
TEST(RoleDecorators, MenuRowColoursByRoleInFull) {
    const Theme full(ColorMode::Full, false);
    for (const RoleColour& rc : kRoleColours) {
        ftxui::Screen screen =
            renderTo(render::menuRow("dev0", false, false, std::nullopt, rc.role, full), {20, 1});
        EXPECT_TRUE(hasRolePixel(screen, rc.fg, rc.dim)) << static_cast<int>(rc.role);
    }
}

// The status bar colours under its reverse video: the pixel keeps the role's
// foreground and is inverted.
TEST(RoleDecorators, StatusBarColoursByRoleInFull) {
    const Theme full(ColorMode::Full, false);
    for (const RoleColour& rc : kRoleColours) {
        ftxui::Screen screen = renderTo(views::renderStatusBar("outcome", rc.role, full), {40, 1});
        EXPECT_TRUE(hasRolePixel(screen, rc.fg, rc.dim, /*inverted=*/true))
            << static_cast<int>(rc.role);
    }
}

// Per-view wiring: a danger outcome paints the status bar red-on-reverse in
// every view (rows carry no role in these fixtures, so the only red is the bar).
TEST(RoleDecorators, DangerStatusWiresIntoEveryView) {
    const Theme full(ColorMode::Full, false);
    const StateCase failure{"failure", [](const Theme&) { return ftxui::text("(idle)"); },
                            "Error: operation failed", Role::Danger, "Error:"};
    for (const auto& build : {&buildDevices, &buildModules, &buildUpdates}) {
        ftxui::Screen screen = renderTo((*build)(failure, full), {120, 32});
        EXPECT_TRUE(hasRolePixel(screen, Color::Red, false, /*inverted=*/true));
    }
    ftxui::Screen snap = renderTo(
        views::renderSnapshotsView(snapshotView(snapshotStates().at(5), full), full), {120, 32});
    EXPECT_TRUE(hasRolePixel(snap, Color::Red, false, /*inverted=*/true));
}

// The Modules security banner carries its own role: info in steady state colours
// the banner blue (and is not reverse-video, distinguishing it from the bar).
TEST(RoleDecorators, ModulesBannerColoursByRoleInFull) {
    const Theme full(ColorMode::Full, false);
    const StateCase steady{"prompt", moduleRows, "Loaded 42 modules.", std::nullopt, "Modules"};
    ftxui::Screen screen = renderTo(buildModules(steady, full), {120, 32});
    EXPECT_TRUE(hasRolePixel(screen, Color::Blue, false));  // info banner
}

}  // namespace
}  // namespace devmgr::tui
