#pragma once

namespace devmgr::gui {

// Composition root for devmgr-gui. Mirrors tui/src/tui_app.cpp::runTuiApp()
// construction and teardown ordering exactly — see the comments there; every
// one of them (widened try, stop-then-drain, declaration order) applies here.
// With "--self-test" in argv: construct the full wiring, run one enumeration
// and rebuild, print the row count to stdout, and exit 0 without showing a
// window (CI runs this under QT_QPA_PLATFORM=offscreen).
int runGuiApp(int argc, char** argv);

}  // namespace devmgr::gui
