#pragma once
#include <functional>

namespace devmgr::tui::nav {

// Predicate over a list's row indices: true iff the row carries a device,
// module, update or snapshot the user can act on; false for bus group headers
// and for the "(no …)" / "(no matches)" placeholder rows.
using Selectable = std::function<bool(int)>;

// Snaps a list's selected index onto a selectable row (pass-2 bug B3: the
// cursor could come to rest on a group header or on the empty-list placeholder,
// where every verb then refused with "no device selected").
//
// `dir` is the direction the cursor was last travelling — +1 down, -1 up, 0
// unknown. The snap continues that way first, so arrow keys read as "skip the
// header", and only then falls back to the opposite direction, so a header at
// the far end of the list still yields a row.
//
// Returns `selected` (clamped) unchanged when NO row is selectable: an empty
// list has no cursor at all, and the caller renders every row unselected.
[[nodiscard]] int snapToSelectable(int selected, int rowCount, int dir,
                                   const Selectable& selectable);

// True iff at least one row is selectable — i.e. iff the list should show a
// cursor at all.
[[nodiscard]] bool anySelectable(int rowCount, const Selectable& selectable);

}  // namespace devmgr::tui::nav
