#pragma once
#include <cstdint>  // std::uint8_t

// The TUI's semantic vocabulary, with no toolkit dependency of any kind.
//
// Split out of theme.hpp/render_util.hpp so the state → role/glyph mapping
// (state_roles.hpp) can be compiled and unit-tested against the app/core enums
// WITHOUT pulling FTXUI in, while the role → decorator half stays in the render
// layer where FTXUI belongs. Nothing here allocates, includes or links.

namespace devmgr::tui {

// Semantic colour roles from docs/DESIGN.md §4.1, mapped to 16-colour ANSI in
// Full mode and to identity (no colour) in Mono/Plain. Meaning never rides on
// colour alone (§10): callers pair every role with a glyph and text.
// Nominal is the resting state of a checked-and-normal row; Success is a
// completed operation on the status line. They are separate roles so an ordinary
// list stays quiet without giving up the affirmative (§4.1).
enum class Role : std::uint8_t { Accent, Nominal, Success, Warning, Danger, Info, Muted };

namespace render {

// Status glyphs (docs/DESIGN.md §8). ASCII is the width-safe default; the
// Unicode dots are an opt-in honoured only in Full mode. A glyph is always
// paired with its state word by the caller, so it never carries meaning alone
// (§10).
enum class Glyph : std::uint8_t {
    Enabled,
    Disabled,
    Unavailable,
    Unsigned,
    Ok,
    Marker,
    Essential,  // '#': unbinding/unloading may make the system unusable
    Important,  // '~': in use or security-enforcing; losing it will be felt
};

// A small badge rendered between the state glyph and the row label, carrying its
// OWN role so it cannot recolour anything else on the row (R4: the criticality
// marker never touches the +/- state glyph or the signed cell, and never uses
// the danger role — a wrong marker must not look like a destructive outcome).
struct Badge {
    Glyph glyph;
    Role role;
};

}  // namespace render
}  // namespace devmgr::tui
