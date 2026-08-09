#pragma once
#include "tui/src/theme.hpp"

namespace devmgr::tui {
// With selfTest: construct the full wiring, run one enumeration, print the
// row count, and exit without entering the alternate screen — the TUI mirror
// of the GUI's --self-test (gui/src/gui_app.hpp).
//
// theme carries the resolved colour capability (full/mono/plain) + glyph
// opt-in; the render layer maps semantic roles through it (docs/DESIGN.md §4.1).
int runTuiApp(bool selfTest = false, const Theme& theme = {});
}  // namespace devmgr::tui
