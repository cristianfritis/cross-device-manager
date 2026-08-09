#pragma once

namespace devmgr::tui::views {

// Width of the LEFT (collection) pane for the three wide views — Modules,
// Updates and Snapshots — whose rows carry long identifiers and so want a wider
// list than Devices does.
//
// Pass 1 and pass 2 both passed a fixed 72 here. That is a hard
// `size(WIDTH, EQUAL, 72)`, so at the DESIGN §3.2 minimum of 80 columns the
// detail pane was left with SIX columns — two of them its own border — and every
// detail line rendered as three characters and an ellipsis ("Mod…", "Ris…").
// The pane was structurally present and informationally empty, which is how the
// R4 colour-independence pairing (marker on the row, criticality WORD in the
// detail pane, both on one screen) silently stopped holding at 80x24 while every
// render test, all of which ran the wide views at 120 columns, stayed green.
//
// The split is therefore computed from the terminal width, reserving a floor for
// the detail pane. At >=108 columns nothing changes (72, as before). Below that
// the list yields, down to 44 at 80 columns — the same 44/34 split the Devices
// tab has always used, so at the minimum size all four tabs share one geometry.
//
// Trade-off at 80 columns, accepted deliberately: a Modules row clips after
// `Name` and `Signed` (B5's two priority columns) rather than after `Size`. Ref,
// Size and Used-by are not lost — the detail pane, now that it can render at
// all, carries them in full for the selected row, along with the criticality
// word that the marker glyph pairs with.
//
// Pure and total: no terminal query, no clamping surprises for absurd inputs.
// The shell reads the live width and passes the result in as view data, so the
// view functions stay pure (docs/DESIGN.md §8).
[[nodiscard]] int wideLeftPaneWidth(int cols);

}  // namespace devmgr::tui::views
