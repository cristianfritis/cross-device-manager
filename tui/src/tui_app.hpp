#pragma once
namespace devmgr::tui {
// With selfTest: construct the full wiring, run one enumeration, print the
// row count, and exit without entering the alternate screen — the TUI mirror
// of the GUI's --self-test (gui/src/gui_app.hpp).
int runTuiApp(bool selfTest = false);
}  // namespace devmgr::tui
