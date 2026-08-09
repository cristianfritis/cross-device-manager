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

// Glyph lives in semantics.hpp (toolkit-free, shared with the state→glyph
// mapping). The Unicode literals below are single narrow (non-ambiguous-width)
// glyphs.
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
        case Glyph::Essential:
            return "#";  // ASCII in every mode: the Unicode warning signs are
        case Glyph::Important:
            return "~";  // ambiguous-width (§8), and these must never reflow a row
    }
    return "?";
}

// Single-row text that never wraps and never overflows: it fills whatever box
// the layout gives it and, when the string is wider than that, keeps the head
// and spends the last cells on a right ellipsis ("…", or "..." in Plain mode so
// no non-ASCII byte is emitted). `ftxui::text` clips a too-long string silently,
// mid-word, with nothing to say content was dropped — which is how a long status
// message read as a sentence that simply stopped (pass-2 bug B6). Embedded
// newlines and tabs are flattened to spaces so the one-row contract
// (docs/DESIGN.md §3.2) holds for any message a ViewModel can produce.
//
// Unlike `ftxui::text` this asks for only one column of width, so a long string
// cannot inflate its parent's layout requirement either.
ftxui::Element elidedText(std::string text, const Theme& theme);

// One menu-list row: the FTXUI default-entry look (a "> "/"  " selection
// marker, bold and reverse video on the selection) with two presentation
// additions layered on — an optional status glyph before the label (the
// width-safe non-colour signal) and an optional semantic role colour. The role
// decorator is identity outside Full mode, so in Mono/Plain the glyph and the
// row's own text carry the meaning and colour never becomes the sole signal
// (§10). `label` is already windowed/trimmed by the caller.
//
// Every selection signal keys off `selected` alone (pass-2 bug B1). Pass 1 took
// the reverse video from FTXUI's `focused_entry`, which the mouse moves
// independently of `selected` — a mouse move then put the marker, the highlight
// and the detail pane on three different rows. `listFocused` says only whether
// the list owns the keyboard, so losing focus to the filter dims the bar
// without ever moving the cursor.
// `badge` is the criticality marker (R4): its own glyph and its own role, in its
// own element, so it colours nothing but itself — the state glyph and the row's
// signature/version cells keep their own semantics either side of it.
inline ftxui::Element menuRow(std::string_view label, bool selected, bool listFocused,
                              std::optional<Glyph> statusGlyph, std::optional<Role> role,
                              const Theme& theme, std::optional<Badge> badge = std::nullopt) {
    using namespace ftxui;
    std::string prefix = selected ? "> " : "  ";
    if (statusGlyph) {
        prefix.append(glyph(*statusGlyph, theme));
        prefix.push_back(' ');
    }
    // B2: ONE selection treatment for every list — accent (cyan) under reverse
    // video, never the row's own state colour. Pass 1 inverted the state colour,
    // so the selection bar was green on an enabled device, red on a disabled one
    // and a different hue again in each view. The selected row's state stays
    // legible through its glyph and its own words (§10), and the detail pane
    // repeats it in full.
    const std::optional<Role> bodyRole = selected ? std::optional{Role::Accent} : role;

    // elidedText, not text: a row wider than its pane elides visibly on the
    // right (R3 — non-selected rows elide, only the selected one reveals)
    // instead of being cut mid-word with no sign anything was dropped.
    Element e;
    if (badge) {
        // Three segments: the prefix, the criticality badge in its own element
        // with its own role, then the label. Splitting the row is what keeps the
        // badge from recolouring the state glyph before it or the
        // signature/version cells inside the label after it (R4). The badge also
        // keeps its warning colour on the selected row — criticality outranks
        // the selection bar, and FTXUI's nested colour decorators render inner-
        // last, so the badge's own role wins over the row's.
        Element mark = text(std::string(glyph(badge->glyph, theme)) + " ") |
                       theme.decorate(badge->role) | bold;
        Element head = text(prefix);
        // xflex: elidedText asks for a single column so it can never inflate a
        // layout, which inside an hbox means it would be GIVEN a single column.
        // The label is the segment that should absorb the row's spare width.
        Element body = elidedText(std::string(label), theme) | ftxui::xflex;
        if (bodyRole) {
            head = head | theme.decorate(*bodyRole);
            body = body | theme.decorate(*bodyRole);
        }
        e = hbox({std::move(head), std::move(mark), std::move(body)});
    } else {
        e = elidedText(prefix + std::string(label), theme);
        if (bodyRole) e = e | theme.decorate(*bodyRole);
    }
    if (selected) {
        e = e | bold;
        if (listFocused) e = e | inverted;
    }
    return e;
}

// ---- Bounded reveal of an overflowing selected row (design Decision 9) ----
//
// docs/DESIGN.md §4.5 forbids idle decoration, so an overflowing name is
// revealed by a FINITE horizontal slide that comes to rest — not the perpetual
// loop pass 1 shipped, which restarted forever and needed an always-on redraw.
// All three pieces are pure functions of an explicit offset, so any point of the
// reveal is renderable off-screen and testable at offset 0 and at offset max;
// the single live time source (the tick counter) lives in the shell, gated to
// "an overflowing row is selected AND the reveal has not yet come to rest".

// How many glyphs `s` overflows a `width`-cell region by — i.e. the largest
// meaningful offset. 0 when the string fits (no reveal runs at all).
[[nodiscard]] int revealMaxOffset(std::string_view s, int width);

// The `width`-cell window of `s` starting `offset` glyphs in; `offset` is
// clamped, so an out-of-range offset can never read past either end. Glyph-based
// so a multi-byte name (e.g. "…Webcam™") never splits mid-codepoint.
[[nodiscard]] std::string revealWindow(std::string_view s, int width, int offset);

// Offset after `tick` ticks: held at 0 through a short lead-in (so the start of
// the name is readable before anything moves), then one glyph per tick, then AT
// REST at `maxOffset` for every later tick. Monotonic and terminating — that is
// what lets the shell stop the ticker instead of animating forever.
[[nodiscard]] int revealOffset(int tick, int maxOffset);

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
