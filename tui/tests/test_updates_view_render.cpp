// Render tests for the Updates tab (docs/DESIGN.md §12.1): rendered to fixed
// screens at the three reference widths, asserting the frame composes and that
// the optional request banner appears only when set. Behaviour-preserving
// extraction from tui_app.cpp — no colour asserted yet (group 4).
#include "tui/src/views/updates_view.hpp"

#include <algorithm>  // std::min
#include <array>
#include <string>

#include <ftxui/dom/node.hpp>  // Render
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>  // string_width

#include <gtest/gtest.h>

#include "tests/fixtures/backend_sentences.hpp"
#include "tui/src/theme.hpp"
#include "tui/src/views/pane_layout.hpp"  // wideLeftPaneWidth

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

// ---------------------------------------------------------------------------
// R5 (task 10.4): the Updates list carries the same muted, non-selectable
// column header.
// ---------------------------------------------------------------------------

// Row index of the first row containing `needle` inside the LIST pane (the
// detail pane shares the same rows on the right).
int listRowOf(const ftxui::Screen& screen, const std::string& needle, int paneWidth = 72) {
    for (int y = 0; y < screen.dimy(); ++y) {
        std::string pane;
        for (int x = 0; x < std::min(paneWidth, screen.dimx()); ++x)
            pane += screen.PixelAt(x, y).character;
        if (pane.find(needle) != std::string::npos) return y;
    }
    return -1;
}

views::UpdatesView viewWithHeader() {
    views::UpdatesView v = sampleView("");
    // Exactly what UpdatesVM::columnHeader() emits.
    v.columnHeader = "Src    Device                         Version      -> New";
    return v;
}

TEST(UpdatesViewRender, ColumnHeaderIsMutedNonSelectableAndAboveTheRows) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderUpdatesView(viewWithHeader(), theme), s);
        const int header = listRowOf(screen, "Version", s.w);
        ASSERT_GE(header, 0) << "@" << s.w;
        EXPECT_LT(header, listRowOf(screen, "Dock firmware", s.w)) << "@" << s.w;
        // Muted is dim, never a hue; and the header can never take the cursor.
        bool anyDim = false;
        for (int x = 0; x < screen.dimx(); ++x) {
            if (screen.PixelAt(x, header).dim) anyDim = true;
            EXPECT_FALSE(screen.PixelAt(x, header).inverted) << "@" << s.w;
        }
        EXPECT_TRUE(anyDim) << "@" << s.w;
        // No cursor at the start of the pane. (A whole-row search for "> " would
        // match the header's own "->" arrow column.)
        EXPECT_NE(screen.PixelAt(1, header).character, ">") << "@" << s.w;
        for (int y = 0; y < screen.dimy(); ++y) {
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
        }
    }
}

TEST(UpdatesViewRender, EightyByTwentyFourBudgetHoldsWithTheHeader) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderUpdatesView(viewWithHeader(), theme), {80, 24});
    EXPECT_TRUE(screenContains(screen, "Updates"));             // tab bar + legend
    EXPECT_TRUE(screenContains(screen, "fwupd 2.0.1"));         // banner
    EXPECT_TRUE(screenContains(screen, "Version"));             // header
    EXPECT_TRUE(screenContains(screen, "Dock firmware 1.2"));   // >= 1 data row
    EXPECT_TRUE(screenContains(screen, "SSD firmware 5.0"));    // and a second
    EXPECT_TRUE(screenContains(screen, "Refreshed updates."));  // status line
}

TEST(UpdatesViewRender, NoHeaderStringRendersNoHeaderRow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen with = renderTo(views::renderUpdatesView(viewWithHeader(), theme), {120, 32});
    ftxui::Screen without = renderTo(views::renderUpdatesView(sampleView(""), theme), {120, 32});
    EXPECT_EQ(listRowOf(without, "Version"), -1);
    // The header costs two rows here (header + rule), since the Updates pane has
    // no filter field to separate it from.
    EXPECT_LT(listRowOf(without, "Dock firmware"), listRowOf(with, "Dock firmware"));
}

// ---------------------------------------------------------------------------
// Backend availability: the translated sentence is primary, the raw detail is
// one keystroke away, and neither depends on colour (backend-availability and
// tui-presentation specs).
// ---------------------------------------------------------------------------

// The sentence a surface is handed for an unreachable fwupd. devmgr_tui_render
// links neither app nor core, so it cannot call the table directly; the shared
// fixture constant is asserted equal to core::unavailabilityText() by
// tests/unit/test_backend_parity.cpp, which is what makes this a parity check
// rather than a second copy of the wording.
constexpr const char* kFwupdSentence = devmgr::tests::kFwupdUnreachableSentence;
constexpr const char* kRawDiagnostic =
    "Firmware updates: org.freedesktop.DBus.Error.ServiceUnknown: The name org.freedesktop.fwupd "
    "was not provided by any .service files";

views::UpdatesView degradedView(bool showDiagnostics) {
    using namespace ftxui;
    views::UpdatesView v = sampleView("");
    v.banner = std::string(kFwupdSentence) + " | Secure Boot: ON · Lockdown: none";
    v.bannerRole = Role::Warning;  // present but not serving — never Danger
    v.bannerGlyph = render::Glyph::Unavailable;
    v.diagnosticLines = {kRawDiagnostic};
    v.showDiagnostics = showDiagnostics;
    return v;
}

TEST(UpdatesViewRender, DegradedBackendShowsGlyphAndSentenceAtEverySizeAndMode) {
    for (const ColorMode mode : {ColorMode::Full, ColorMode::Mono}) {
        const Theme theme(mode, false);
        for (Size s : kSizes) {
            ftxui::Screen screen =
                renderTo(views::renderUpdatesView(degradedView(false), theme), s);
            EXPECT_TRUE(screenContains(screen, "?")) << "@" << s.w;  // documented unavailable glyph
            // The sentence elides on the narrow screens, so assert the head of it.
            EXPECT_TRUE(screenContains(screen, "Firmware updates unavailable")) << "@" << s.w;
            for (int y = 0; y < screen.dimy(); ++y)
                EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w) << "@" << s.w;
            // Unavailability is a state of the source, never a failed operation:
            // no cell on the screen may be painted danger.
            for (int y = 0; y < screen.dimy(); ++y)
                for (int x = 0; x < screen.dimx(); ++x)
                    EXPECT_NE(screen.PixelAt(x, y).foreground_color,
                              ftxui::Color(ftxui::Color::Red))
                        << "@" << s.w << " " << x << "," << y;
        }
    }
}

TEST(UpdatesViewRender, ClosedDiagnosticsLeakNoRawDetail) {
    for (const ColorMode mode : {ColorMode::Full, ColorMode::Mono, ColorMode::Plain}) {
        const Theme theme(mode, false);
        for (Size s : kSizes) {
            ftxui::Screen screen =
                renderTo(views::renderUpdatesView(degradedView(false), theme), s);
            for (const char* raw :
                 {"org.freedesktop", "DBus.Error", "ServiceUnknown", "errno", "/var/lib"})
                EXPECT_FALSE(screenContains(screen, raw)) << raw << " @" << s.w;
            EXPECT_FALSE(screenContains(screen, "-- Diagnostics --")) << "@" << s.w;
        }
    }
}

TEST(UpdatesViewRender, OpenDiagnosticsRevealTheRawDetailWithoutOverflow) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen screen = renderTo(views::renderUpdatesView(degradedView(true), theme), {80, 24});
    EXPECT_TRUE(screenContains(screen, "-- Diagnostics --"));
    EXPECT_TRUE(screenContains(screen, "org.freedesktop"));  // demoted, not deleted
    // A raw string far wider than 80 columns elides rather than reflowing the
    // layout, and the status line keeps the bottom edge.
    for (int y = 0; y < screen.dimy(); ++y) EXPECT_LE(ftxui::string_width(rowText(screen, y)), 80);
    EXPECT_TRUE(screenContains(screen, "Refreshed updates."));
}

TEST(UpdatesViewRender, DiagnosticsKeyIsListedOnlyWhileSomethingIsDegraded) {
    const Theme theme(ColorMode::Full, false);
    ftxui::Screen healthy = renderTo(views::renderUpdatesView(sampleView(""), theme), {120, 32});
    EXPECT_FALSE(screenContains(healthy, "i=diagnostics"));

    ftxui::Screen degraded =
        renderTo(views::renderUpdatesView(degradedView(false), theme), {120, 32});
    EXPECT_TRUE(screenContains(degraded, "i=diagnostics"));
    EXPECT_TRUE(screenContains(degraded, "q=quit)"));  // still one legend, not two
}

// §10: the state must survive the loss of colour with its words unchanged.
TEST(UpdatesViewRender, SentenceIsByteIdenticalAcrossColourModes) {
    std::string full;
    std::string mono;
    std::string plain;
    const auto bannerRow = [](const ftxui::Screen& screen) {
        for (int y = 0; y < screen.dimy(); ++y) {
            const std::string row = rowText(screen, y);
            if (row.find("Firmware updates unavailable") != std::string::npos) return row;
        }
        return std::string{};
    };
    full = bannerRow(renderTo(
        views::renderUpdatesView(degradedView(false), Theme(ColorMode::Full, false)), {120, 32}));
    mono = bannerRow(renderTo(
        views::renderUpdatesView(degradedView(false), Theme(ColorMode::Mono, false)), {120, 32}));
    plain = bannerRow(renderTo(
        views::renderUpdatesView(degradedView(false), Theme(ColorMode::Plain, false)), {120, 32}));
    ASSERT_FALSE(full.empty());
    EXPECT_EQ(full, mono);
    EXPECT_EQ(full, plain);
    EXPECT_NE(full.find('?'), std::string::npos);  // glyph, not colour, carries it

    // And in Mono nothing on the screen is coloured at all.
    ftxui::Screen monoScreen = renderTo(
        views::renderUpdatesView(degradedView(false), Theme(ColorMode::Mono, false)), {120, 32});
    for (int y = 0; y < monoScreen.dimy(); ++y)
        for (int x = 0; x < monoScreen.dimx(); ++x)
            EXPECT_EQ(monoScreen.PixelAt(x, y).foreground_color, ftxui::Color());
}

// ---------------------------------------------------------------------------
// tab-contextual-toolbar: the legend advertises only keys with a target.
// `d=dismiss` has nothing to act on until a request exists — a dead shortcut,
// unlike `u=install`, which a guard may refuse and therefore stays listed and
// explains itself on the status line.
// ---------------------------------------------------------------------------

// The same sample, told the real terminal width so the legend is actually fitted
// to it (the tests above leave terminalWidth 0 = unbounded).
views::UpdatesView sizedView(const std::string& requestBanner, Size s) {
    views::UpdatesView v = sampleView(requestBanner);
    v.terminalWidth = s.w;
    v.leftPaneWidth = views::wideLeftPaneWidth(s.w);
    return v;
}

TEST(UpdatesViewRender, DismissKeyUnlistedWithoutARequestAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(views::renderUpdatesView(sizedView("", s), theme), s);
        EXPECT_FALSE(screenContains(screen, "d=dismiss")) << "size " << s.w << "x" << s.h;
        // The keys that still have a target remain, and the way out survives the
        // legend's abridgement at the minimum size.
        EXPECT_TRUE(screenContains(screen, "u=install")) << "size " << s.w;
        EXPECT_TRUE(screenContains(screen, "q=quit")) << "size " << s.w;
        for (int y = 0; y < screen.dimy(); ++y)
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
    }
}

TEST(UpdatesViewRender, DismissKeyListedWithARequestAtEverySize) {
    const Theme theme(ColorMode::Full, false);
    for (Size s : kSizes) {
        ftxui::Screen screen = renderTo(
            views::renderUpdatesView(sizedView("unplug and replug the device", s), theme), s);
        EXPECT_TRUE(screenContains(screen, "d=dismiss")) << "size " << s.w << "x" << s.h;
        EXPECT_TRUE(screenContains(screen, "q=quit")) << "size " << s.w;
        for (int y = 0; y < screen.dimy(); ++y)
            EXPECT_LE(ftxui::string_width(rowText(screen, y)), s.w);
    }
}

// A refusable key is NOT the same case: `u=install` stays discoverable even when
// nothing installable is selected, because the refusal is what the user needs to
// read (DESIGN.md §5.3).
TEST(UpdatesViewRender, RefusableKeysStayListed) {
    const Theme theme(ColorMode::Full, false);
    views::UpdatesView v = sizedView("", {80, 24});
    v.list = ftxui::text("(no updates available)");  // nothing selectable at all
    ftxui::Screen screen = renderTo(views::renderUpdatesView(std::move(v), theme), {80, 24});
    EXPECT_TRUE(screenContains(screen, "u=install"));
    EXPECT_TRUE(screenContains(screen, "r=refresh"));
}

}  // namespace
}  // namespace devmgr::tui
