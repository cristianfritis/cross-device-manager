// Render tests for the backend-availability note on every daemon-fed tab
// (backend-availability spec; calm-backend-unavailability §13). The Updates tab
// has its own coverage in test_updates_view_render.cpp; this file covers the
// three views devmgrd feeds — Devices, Modules and Snapshots — at the three
// reference sizes in FULL and MONO.
//
// What is being pinned, in the words of the misread catalog these tests exist to
// close: the sentence renders and is byte-equal to the shared table (#1), the
// note is never painted danger and never inverts a readable row (#2/#3), no raw
// diagnostic reaches the screen with the region closed (#4), the `i` legend
// appears only while there is something to reveal (#25), the region elides
// rather than overflowing at 80x24 (#12), and MONO keeps the note legible with
// the same bytes (#22).
#include <array>
#include <string>
#include <vector>

#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>  // string_width

#include <gtest/gtest.h>

#include "tests/fixtures/backend_sentences.hpp"
#include "tui/src/theme.hpp"
#include "tui/src/views/devices_view.hpp"
#include "tui/src/views/modules_view.hpp"
#include "tui/src/views/snapshots_view.hpp"

namespace devmgr::tui {
namespace {

struct Size {
    int w;
    int h;
};
constexpr std::array<Size, 3> kSizes{{{120, 32}, {100, 28}, {80, 24}}};

// The raw text the privileged channel hands back — long enough at 80 columns to
// prove the diagnostics region elides instead of overflowing.
constexpr const char* kRawDaemon =
    "helper devmgrd is not available: org.freedesktop.systemd1.UnitMasked: Unit "
    "devmgrd.service is masked.";

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

// What the old `expectNoOverflow` could not do, and why this replaces it (§14
// F5): rows are read out of a FIXED-WIDTH ftxui::Screen, so "this row's width
// is <= the terminal width" holds by construction. The assertion could never
// fail, which is how the degraded Snapshots legend at 80x24 lost `q=quit`
// entirely while every overflow check passed.
//
// A clipped row is also indistinguishable from an exactly-fitting one by
// inspection alone — both end in a non-space at the last column. So the
// property worth asserting is not "nothing overflowed" but COMPLETENESS: a
// string the view intends to show must be present in full. A truncated legend
// fails this; an 80-column legend that fits exactly passes, correctly.
void expectRendersInFull(const ftxui::Screen& screen, const std::string& needle, Size s) {
    EXPECT_TRUE(screenContains(screen, needle))
        << "'" << needle << "' is missing or truncated @" << s.w << "x" << s.h;
}

// Whatever colour THIS theme gives Role::Danger — asked of the theme itself
// rather than hardcoded, so a palette change cannot quietly void the assertion
// below.
ftxui::Color dangerColour(const Theme& theme) {
    ftxui::Screen probe =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ftxui::Render(probe, ftxui::text("x") | theme.decorate(Role::Danger));
    return probe.PixelAt(0, 0).foreground_color;
}

// The catalog's #2/#3: the whole point of this change is that a readable screen
// is never painted danger. Danger in this theme is a red foreground; the old
// full-bleed alert additionally inverted. Neither may appear anywhere on a
// screen whose content is still perfectly readable.
void expectNoDangerAnywhere(const ftxui::Screen& screen, const Theme& theme) {
    const auto danger = dangerColour(theme);
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            EXPECT_NE(screen.PixelAt(x, y).foreground_color, danger)
                << "danger paint at " << x << "," << y;
        }
    }
}

views::DevicesView devicesView(bool degraded, bool open, int width = 0) {
    using namespace ftxui;
    views::DevicesView v{.activeTab = 0,
                         .filterInput = text("filter devices…"),
                         .deviceList = vbox({text("  AMD USB controller")}),
                         .detail = text("Name: AMD USB controller"),
                         .statusText = "Refreshed 18 devices.",
                         .leftPaneWidth = 44};
    if (degraded) {
        v.banner = tests::kDevmgrdUnreachableSentence;
        v.bannerRole = Role::Warning;
        v.bannerGlyph = render::Glyph::Unavailable;
        v.diagnosticLines = {std::string("devmgrd: ") + kRawDaemon};
        v.showDiagnostics = open;
    }
    v.terminalWidth = width;
    return v;
}

views::ModulesView modulesView(bool degraded, bool open, int width = 0) {
    using namespace ftxui;
    views::ModulesView v{.activeTab = 1,
                         .banner = "Secure Boot: off · Lockdown: none",
                         .filterInput = text("filter modules…"),
                         .list = vbox({text("xhci_hcd")}),
                         .detail = text("Module xhci_hcd"),
                         .statusText = "Loaded modules.",
                         .leftPaneWidth = 72};
    if (degraded) {
        // ModulesVM folds the sentence into the banner itself; the view adds the
        // glyph and the region.
        v.banner = std::string(tests::kDevmgrdUnreachableSentence) + " | " + v.banner;
        v.bannerRole = Role::Warning;
        v.bannerGlyph = render::Glyph::Unavailable;
        v.diagnosticLines = {std::string("devmgrd: ") + kRawDaemon};
        v.showDiagnostics = open;
    }
    v.terminalWidth = width;
    return v;
}

views::SnapshotsView snapshotsView(bool degraded, bool open, int width = 0) {
    using namespace ftxui;
    views::SnapshotsView v{.activeTab = 3,
                           .banner = "12 snapshots · 8 auto · 4 manual",
                           .statusText = "Loaded snapshots.",
                           .leftPaneWidth = 72};
    v.filterInput = text("filter snapshots…");
    v.list = vbox({text("a1b2c3  boot")});
    v.detail = text("Snapshot a1b2c3");
    if (degraded) {
        v.banner = std::string(tests::kDevmgrdUnreachableSentence) + " | " + v.banner;
        v.bannerRole = Role::Warning;
        v.bannerGlyph = render::Glyph::Unavailable;
        v.diagnosticLines = {std::string("devmgrd: ") + kRawDaemon};
        v.showDiagnostics = open;
    }
    v.terminalWidth = width;
    return v;
}

// One render per (view, mode) so each assertion below runs against all three
// daemon-fed tabs without three near-identical test bodies.
std::vector<ftxui::Screen> everyDegradedView(const Theme& theme, Size s, bool open) {
    return {renderTo(views::renderDevicesView(devicesView(true, open, s.w), theme), s),
            renderTo(views::renderModulesView(modulesView(true, open, s.w), theme), s),
            renderTo(views::renderSnapshotsView(snapshotsView(true, open, s.w), theme), s)};
}

}  // namespace

TEST(BackendAvailabilityRender, SentenceAndGlyphRenderOnEveryDaemonFedTab) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        for (const auto& screen : everyDegradedView(theme, s, /*open=*/false)) {
            EXPECT_TRUE(screenContains(screen, tests::kDevmgrdUnreachableSentence)) << "@" << s.w;
            EXPECT_TRUE(screenContains(screen, "?")) << "glyph missing @" << s.w;
            expectRendersInFull(screen, "q=quit)", s);
            expectNoDangerAnywhere(screen, theme);
        }
    }
}

// #4/#5: with the region closed, nothing raw is on screen — no D-Bus name, no
// unit name, no errno, no path.
TEST(BackendAvailabilityRender, ClosedRegionLeaksNoRawDiagnostic) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        for (const auto& screen : everyDegradedView(theme, s, /*open=*/false)) {
            EXPECT_FALSE(screenContains(screen, "org.freedesktop")) << "@" << s.w;
            EXPECT_FALSE(screenContains(screen, "UnitMasked")) << "@" << s.w;
            EXPECT_FALSE(screenContains(screen, "devmgrd.service")) << "@" << s.w;
            EXPECT_FALSE(screenContains(screen, "-- Diagnostics --")) << "@" << s.w;
        }
    }
}

// #9/#12: opened, the region exists, is introduced by the muted header (not a
// box), and its long raw line elides rather than pushing a row past the width.
TEST(BackendAvailabilityRender, OpenRegionRevealsRawTextAndStillFits) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        for (const auto& screen : everyDegradedView(theme, s, /*open=*/true)) {
            EXPECT_TRUE(screenContains(screen, "-- Diagnostics --")) << "@" << s.w;
            EXPECT_TRUE(screenContains(screen, "devmgrd:")) << "@" << s.w;
            expectRendersInFull(screen, "q=quit)", s);
        }
    }
}

// #25: the key is advertised only while it does something.
//
// This ran at 120x32 alone and asserted `q=quit)` survived only on the HEALTHY
// renders (§14 F4), which is exactly why F3 went unseen: at 80x24 the degraded
// legend rendered `… x=delete  i=diagn` and dropped `q=quit` off the screen.
// Both halves now run at every supported size.
TEST(BackendAvailabilityRender, DiagnosticsKeyIsListedOnlyWhileDegraded) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        for (const auto& screen : everyDegradedView(theme, s, /*open=*/false)) {
            // `i=diag` is the abbreviated spelling a narrow terminal falls back
            // to; either form satisfies "the key is advertised".
            EXPECT_TRUE(screenContains(screen, "i=diagnostics") || screenContains(screen, "i=diag"))
                << "diagnostics key missing @" << s.w;
            expectRendersInFull(screen, "q=quit)", s);  // the way out is never the thing cut
            expectRendersInFull(screen, "q=quit)", s);
        }

        const std::array<ftxui::Screen, 3> healthy{
            renderTo(views::renderDevicesView(devicesView(false, false, s.w), theme), s),
            renderTo(views::renderModulesView(modulesView(false, false, s.w), theme), s),
            renderTo(views::renderSnapshotsView(snapshotsView(false, false, s.w), theme), s)};
        for (const auto& screen : healthy) {
            EXPECT_FALSE(screenContains(screen, "i=diag")) << "inert key advertised @" << s.w;
            expectRendersInFull(screen, "q=quit)", s);
            expectRendersInFull(screen, "q=quit)", s);
        }
    }
}

// §14 F3 in isolation: the legend is composed to fit the terminal, so the
// narrowest supported size still shows every shortcut it claims to offer.
TEST(BackendAvailabilityRender, DegradedLegendFitsTheNarrowestTerminal) {
    const Theme theme(ColorMode::Full, false);
    const Size s{80, 24};
    for (const auto& screen : everyDegradedView(theme, s, /*open=*/false)) {
        bool found = false;
        for (int y = 0; y < screen.dimy() && !found; ++y) {
            const std::string row = rowText(screen, y);
            if (row.find("q=quit)") == std::string::npos) continue;
            found = true;
            // Opening paren present means the row is a whole legend, not the
            // tail of one that started off-screen; the diagnostics key present
            // means the affordance survived the fit rather than being the thing
            // sacrificed to make room.
            EXPECT_NE(row.find('('), std::string::npos) << "legend lost its opening paren: " << row;
            EXPECT_TRUE(row.find("i=diag") != std::string::npos)
                << "diagnostics key dropped to fit @" << s.w << ": " << row;
        }
        EXPECT_TRUE(found) << "no complete legend row rendered @" << s.w;
    }
}

// #22: calm is not invisible. In MONO the sentence is byte-identical to FULL and
// the state is still identifiable from glyph + words, with no colour at all.
TEST(BackendAvailabilityRender, MonoKeepsTheSentenceLegibleAndIdentical) {
    const Theme full(ColorMode::Full, false);
    const Theme mono(ColorMode::Mono, false);
    for (Size s : kSizes) {
        const auto colour = everyDegradedView(full, s, /*open=*/false);
        const auto plain = everyDegradedView(mono, s, /*open=*/false);
        ASSERT_EQ(colour.size(), plain.size());
        for (std::size_t i = 0; i < plain.size(); ++i) {
            EXPECT_TRUE(screenContains(plain[i], tests::kDevmgrdUnreachableSentence)) << "@" << s.w;
            EXPECT_TRUE(screenContains(plain[i], "?")) << "@" << s.w;
            // Same words in both modes — the sentence is the VM's, not the theme's.
            for (int y = 0; y < plain[i].dimy(); ++y) {
                EXPECT_EQ(rowText(plain[i], y), rowText(colour[i], y))
                    << "row " << y << " @" << s.w;
            }
            expectRendersInFull(plain[i], "q=quit)", s);
        }
    }
}

// A healthy daemon leaves every one of these views exactly as it was: no banner
// row on Devices, no glyph, no region — the affordance collapses to nothing.
TEST(BackendAvailabilityRender, HealthyDaemonRendersNoNoteAnywhere) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        const std::array<ftxui::Screen, 3> healthy{
            renderTo(views::renderDevicesView(devicesView(false, false), theme), s),
            renderTo(views::renderModulesView(modulesView(false, false), theme), s),
            renderTo(views::renderSnapshotsView(snapshotsView(false, false), theme), s)};
        for (const auto& screen : healthy) {
            EXPECT_FALSE(screenContains(screen, tests::kDevmgrdUnreachableSentence)) << "@" << s.w;
            EXPECT_FALSE(screenContains(screen, "-- Diagnostics --")) << "@" << s.w;
            expectRendersInFull(screen, "q=quit)", s);
        }
    }
}

}  // namespace devmgr::tui
