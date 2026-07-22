// Render tests for the TUI theme (docs/DESIGN.md §4.1, §12.1): role -> colour
// mapping, capability downgrade to identity, and flag/env resolution. Colours
// are proven by rendering to a fixed screen and reading back pixel attributes,
// not by inspecting decorators directly.
#include "tui/src/theme.hpp"

#include <cstdlib>  // setenv, unsetenv

#include <ftxui/dom/elements.hpp>  // text, operator|
#include <ftxui/dom/node.hpp>      // Render
#include <ftxui/screen/color.hpp>  // Color
#include <ftxui/screen/screen.hpp>

#include <gtest/gtest.h>

#include "tui/src/render_util.hpp"

namespace devmgr::tui {
namespace {

using ftxui::Color;

// Renders a single glyph decorated by `role` and returns pixel (0,0).
ftxui::Pixel renderRole(const Theme& theme, Role role) {
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(1), ftxui::Dimension::Fixed(1));
    ftxui::Element el = ftxui::text("x") | theme.decorate(role);
    ftxui::Render(screen, el);
    return screen.PixelAt(0, 0);
}

TEST(ThemeClassify, StrictestDegradeWins) {
    EXPECT_EQ(Theme::classify(false, false, false), ColorMode::Full);
    EXPECT_EQ(Theme::classify(true, false, false), ColorMode::Mono);
    EXPECT_EQ(Theme::classify(false, true, false), ColorMode::Plain);
    EXPECT_EQ(Theme::classify(false, false, true), ColorMode::Plain);
    // Plain beats Mono; any Plain signal wins.
    EXPECT_EQ(Theme::classify(true, true, false), ColorMode::Plain);
    EXPECT_EQ(Theme::classify(true, false, true), ColorMode::Plain);
}

TEST(ThemeDecorate, FullModeMapsRolesToAnsiColours) {
    const Theme full(ColorMode::Full, false);
    EXPECT_EQ(renderRole(full, Role::Accent).foreground_color, Color::Cyan);
    EXPECT_EQ(renderRole(full, Role::Success).foreground_color, Color::Green);
    EXPECT_EQ(renderRole(full, Role::Warning).foreground_color, Color::Yellow);
    EXPECT_EQ(renderRole(full, Role::Danger).foreground_color, Color::Red);
    EXPECT_EQ(renderRole(full, Role::Info).foreground_color, Color::Blue);
    // Muted is the dim attribute, not a hue.
    const ftxui::Pixel muted = renderRole(full, Role::Muted);
    EXPECT_EQ(muted.foreground_color, Color::Default);
    EXPECT_TRUE(muted.dim);
}

TEST(ThemeDecorate, MonoAndPlainAreIdentity) {
    for (ColorMode mode : {ColorMode::Mono, ColorMode::Plain}) {
        const Theme theme(mode, false);
        // No colour applied for any role.
        EXPECT_EQ(renderRole(theme, Role::Success).foreground_color, Color::Default);
        EXPECT_EQ(renderRole(theme, Role::Danger).foreground_color, Color::Default);
        EXPECT_EQ(renderRole(theme, Role::Accent).foreground_color, Color::Default);
        // Muted no longer dims once colour is degraded.
        EXPECT_FALSE(renderRole(theme, Role::Muted).dim);
    }
}

TEST(ThemeResolve, EnvAndFlags) {
    unsetenv("NO_COLOR");
    unsetenv("TERM");
    EXPECT_EQ(Theme::resolve(false, false, false).mode(), ColorMode::Full);

    setenv("NO_COLOR", "1", 1);
    EXPECT_EQ(Theme::resolve(false, false, false).mode(), ColorMode::Mono);

    // Empty NO_COLOR does not count (no-color.org: present and non-empty).
    setenv("NO_COLOR", "", 1);
    EXPECT_EQ(Theme::resolve(false, false, false).mode(), ColorMode::Full);
    unsetenv("NO_COLOR");

    setenv("TERM", "dumb", 1);
    EXPECT_EQ(Theme::resolve(false, false, false).mode(), ColorMode::Plain);
    unsetenv("TERM");

    // Flags stand in for the env signals.
    EXPECT_EQ(Theme::resolve(true, false, false).mode(), ColorMode::Mono);
    EXPECT_EQ(Theme::resolve(false, true, false).mode(), ColorMode::Plain);
}

TEST(ThemeResolve, UnicodeGlyphsOnlyInFullMode) {
    EXPECT_TRUE(Theme::resolve(false, false, true).unicodeGlyphs());    // Full + opt-in
    EXPECT_FALSE(Theme::resolve(false, false, false).unicodeGlyphs());  // Full, no opt-in
    EXPECT_FALSE(Theme::resolve(true, false, true).unicodeGlyphs());    // Mono suppresses opt-in
    EXPECT_FALSE(Theme::resolve(false, true, true).unicodeGlyphs());    // Plain suppresses opt-in
}

TEST(RenderUtilGlyph, AsciiDefaultUnicodeOptIn) {
    const Theme ascii(ColorMode::Full, false);
    EXPECT_EQ(render::glyph(render::Glyph::Enabled, ascii), "+");
    EXPECT_EQ(render::glyph(render::Glyph::Disabled, ascii), "-");
    EXPECT_EQ(render::glyph(render::Glyph::Unavailable, ascii), "?");
    EXPECT_EQ(render::glyph(render::Glyph::Unsigned, ascii), "!");

    const Theme uni(ColorMode::Full, true);
    EXPECT_EQ(render::glyph(render::Glyph::Enabled, uni), "●");

    // Mono/Plain always ASCII even if a unicode flag leaked through.
    const Theme mono(ColorMode::Mono, true);
    EXPECT_EQ(render::glyph(render::Glyph::Enabled, mono), "+");
}

// Returns true iff every rendered cell is a single-byte ASCII character.
bool screenIsAscii(const ftxui::Screen& screen) {
    for (int y = 0; y < screen.dimy(); ++y) {
        for (int x = 0; x < screen.dimx(); ++x) {
            for (unsigned char c : screen.PixelAt(x, y).character) {
                if (c > 0x7F) return false;
            }
        }
    }
    return true;
}

TEST(RenderUtilFrame, PlainModeEmitsNoNonAscii) {
    const Theme plain(ColorMode::Plain, false);
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(6), ftxui::Dimension::Fixed(3));
    ftxui::Element el = render::regionFrame(ftxui::text("x"), plain);
    ftxui::Render(screen, el);
    EXPECT_TRUE(screenIsAscii(screen));
}

TEST(RenderUtilFrame, FullModeUsesBoxDrawing) {
    const Theme full(ColorMode::Full, false);
    ftxui::Screen screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(6), ftxui::Dimension::Fixed(3));
    ftxui::Element el = render::regionFrame(ftxui::text("x"), full);
    ftxui::Render(screen, el);
    EXPECT_FALSE(screenIsAscii(screen));  // Unicode box characters present
}

}  // namespace
}  // namespace devmgr::tui
