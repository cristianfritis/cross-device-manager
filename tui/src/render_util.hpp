#pragma once
#include <cstdint>      // std::uint8_t
#include <string>       // std::string
#include <string_view>  // std::string_view
#include <utility>      // std::move

#include <ftxui/dom/elements.hpp>  // Element, hbox, text, size, border, separator
#include <ftxui/screen/pixel.hpp>  // Pixel

#include "tui/src/theme.hpp"

namespace devmgr::tui::render {

// Status glyphs (docs/DESIGN.md §8). ASCII is the width-safe default; the
// Unicode dots are an opt-in honoured only in Full mode. A glyph is always
// paired with its state word by the caller, so it never carries meaning alone
// (§10). The Unicode literals are single narrow (non-ambiguous-width) glyphs.
enum class Glyph : std::uint8_t { Enabled, Disabled, Unavailable, Unsigned, Ok, Marker };

inline std::string_view glyph(Glyph g, const Theme& theme) {
    const bool u = theme.unicodeGlyphs();
    switch (g) {
        case Glyph::Enabled:
            return u ? "●" : "+";  // filled circle / plus
        case Glyph::Disabled:
            return u ? "○" : "-";  // hollow circle / minus
        case Glyph::Unavailable:
            return u ? "◌" : "?";  // dotted circle / question
        case Glyph::Unsigned:
            return u ? "◉" : "!";  // fisheye / bang
        case Glyph::Ok:
            return u ? "●" : "+";  // filled circle / plus
        case Glyph::Marker:
            return u ? "◆" : "*";  // diamond / star (HEAD, last-good)
    }
    return "?";
}

// Detail key/value row with a fixed-width label column (§5.2: every value
// starts in the same place, no value abuts its colon).
inline ftxui::Element kvRow(std::string_view label, std::string_view value, int labelWidth) {
    using namespace ftxui;
    return hbox({
        text(std::string(label)) | size(WIDTH, EQUAL, labelWidth),
        text("  "),
        text(std::string(value)) | flex,
    });
}

// Major-region frame (§4.3: borders on major regions only). Unicode box border
// in Full/Mono; an all-ASCII '+' frame in Plain so no non-ASCII byte is emitted.
inline ftxui::Element regionFrame(ftxui::Element inner, const Theme& theme) {
    using namespace ftxui;
    if (theme.asciiBorders()) {
        Pixel p;
        p.character = "+";
        return inner | borderWith(p);
    }
    return border(std::move(inner));
}

// Horizontal separator that degrades to an ASCII '-' rule in Plain mode.
inline ftxui::Element hsep(const Theme& theme) {
    using namespace ftxui;
    if (theme.asciiBorders()) {
        Pixel p;
        p.character = "-";
        return separator(p);
    }
    return separator();
}

}  // namespace devmgr::tui::render
