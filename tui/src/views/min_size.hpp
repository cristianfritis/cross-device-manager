#pragma once
#include <ftxui/dom/elements.hpp>  // Element

namespace devmgr::tui::views {

// docs/DESIGN.md §3.2 minimum-size guard. Below this the master-detail split
// cannot render without writing outside the screen, so the shell shows a
// concise notice instead of a broken layout. 80x24 is the smallest of the three
// reference sizes the render tests target (§12.1).
inline constexpr int kMinCols = 80;
inline constexpr int kMinRows = 24;

// True when the terminal is too small for the full layout — the toggle the
// shell keys the notice off (K3). Pure integer comparison, so the boundary is
// testable off-screen without a Terminal.
bool belowMinimumSize(int cols, int rows);

// The concise "resize or quit" notice shown while belowMinimumSize() holds.
// Pure and side-effect free: centred ASCII text naming the minimum and the two
// ways out (resize, or press q). No colour — a too-small terminal is not a
// state, so it needs no role.
ftxui::Element renderMinSizeNotice();

}  // namespace devmgr::tui::views
