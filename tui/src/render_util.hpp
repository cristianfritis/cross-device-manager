#pragma once
#include <cstdint>      // std::uint8_t
#include <optional>     // std::optional
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

// One menu-list row, byte-for-byte the FTXUI default-entry look (a "> "/"  "
// selection marker, reverse video when focused, bold when active) with two
// presentation additions layered on: an optional status glyph before the label
// (the width-safe non-colour signal) and an optional semantic role colour. The
// role decorator is identity outside Full mode, so in Mono/Plain the glyph and
// the row's own text carry the meaning and colour never becomes the sole signal
// (§10). `label` is already windowed/marquee-trimmed by the caller.
inline ftxui::Element menuRow(std::string_view label, bool active, bool focused,
                              std::optional<Glyph> statusGlyph, std::optional<Role> role,
                              const Theme& theme) {
    using namespace ftxui;
    std::string line = active ? "> " : "  ";
    if (statusGlyph) {
        line.append(glyph(*statusGlyph, theme));
        line.push_back(' ');
    }
    line.append(label);
    Element e = text(line);
    if (role) e = e | theme.decorate(*role);
    if (focused) e = e | inverted;
    if (active) e = e | bold;
    return e;
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
