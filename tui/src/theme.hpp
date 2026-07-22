#pragma once
#include <cstdint>                 // std::uint8_t
#include <ftxui/dom/elements.hpp>  // Decorator, color, dim, nothing
#include <ftxui/screen/color.hpp>  // Color

namespace devmgr::tui {

// Terminal colour capability, resolved once at startup and never changed after.
// Ordered by how much it degrades output (Full < Mono < Plain):
//   Full  — 16-colour ANSI + optional Unicode glyphs and box-drawing borders.
//   Mono  — no colour (role decorators become identity), ASCII glyphs, Unicode
//           box-drawing still allowed. Triggered by NO_COLOR / --no-color.
//   Plain — no colour and ASCII everything, borders and separators included.
//           Triggered by --ascii / TERM=dumb.
enum class ColorMode : std::uint8_t { Full, Mono, Plain };

// Semantic colour roles from docs/DESIGN.md §4.1, mapped to 16-colour ANSI in
// Full mode and to identity (no colour) in Mono/Plain. Meaning never rides on
// colour alone (§10): callers pair every role with a glyph and text.
enum class Role : std::uint8_t { Accent, Success, Warning, Danger, Info, Muted };

// Presentation-only: maps semantic roles to FTXUI decorators and reports the
// active capability so views can pick ASCII vs Unicode glyphs/borders. Holds no
// application state; safe to copy and pass by value into pure render functions.
class Theme {
   public:
    Theme() = default;
    // Unicode glyphs are clamped to Full mode here so the invariant holds no
    // matter how a Theme is constructed (direct or via resolve()).
    Theme(ColorMode mode, bool unicodeGlyphs)
        : mode_(mode), unicodeGlyphs_(unicodeGlyphs && mode == ColorMode::Full) {}

    // Reads NO_COLOR/TERM from the environment, combines them with the CLI
    // flags, and classifies. unicodeOptIn is honoured only in Full mode (§8
    // width-safety default is ASCII).
    [[nodiscard]] static Theme resolve(bool noColorFlag, bool asciiFlag, bool unicodeOptIn);

    // Pure capability classifier (no environment access) — the testable core of
    // resolve(). Plain wins over Mono wins over Full, so the strictest degrade
    // requested by any signal is the one applied.
    [[nodiscard]] static ColorMode classify(bool noColor, bool ascii, bool termDumb);

    [[nodiscard]] ColorMode mode() const { return mode_; }
    [[nodiscard]] bool colored() const { return mode_ == ColorMode::Full; }
    [[nodiscard]] bool asciiBorders() const { return mode_ == ColorMode::Plain; }
    [[nodiscard]] bool unicodeGlyphs() const { return unicodeGlyphs_; }

    // Decorator for a semantic role. Identity outside Full mode, so applying it
    // in Mono/Plain leaves the element's colour untouched.
    [[nodiscard]] ftxui::Decorator decorate(Role role) const;

   private:
    ColorMode mode_ = ColorMode::Full;
    bool unicodeGlyphs_ = false;
};

}  // namespace devmgr::tui
