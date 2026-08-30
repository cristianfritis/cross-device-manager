#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

#include "devmgr/core/version.hpp"
#include "gui/src/gui_app.hpp"

#ifdef _WIN32
#include <windows.h>

#include <cstdio>
#include <ios>

namespace {

// On Windows devmgr-gui links as a GUI-subsystem binary (gui/CMakeLists.txt), so
// the loader attaches no console to it — which is the point: a console-subsystem
// Qt app pops a stray empty conhost window beside its own on every launch. The
// cost is that `--version` and `--self-test` would print into nothing, so the
// terminal output those two contracts promise has to be reattached by hand.
//
// The order below matters. A launcher that already handed us somewhere to write
// — CTest's pipe, a shell redirect, a console shell that passed its handles down
// — has valid standard handles, and stealing them back onto CONOUT$ would send
// the output to the console INSTEAD of to the pipe the caller is reading. So the
// existing handle wins and this function does nothing. Only when there is no
// output handle at all do we look for the console that launched us; from
// Explorer there is none, AttachConsole fails, and the program stays windowed.
void attachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0) return;
    FILE* stream = nullptr;
    (void)freopen_s(&stream, "CONOUT$", "w", stdout);
    (void)freopen_s(&stream, "CONOUT$", "w", stderr);
    (void)freopen_s(&stream, "CONIN$", "r", stdin);
    // cout/cerr hold the FILE* buffers captured at startup, when there was no
    // console; re-syncing rebinds them to the ones freopen_s just opened.
    std::ios::sync_with_stdio(true);
}

}  // namespace
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    attachParentConsole();  // before any std::cout/std::cerr below
#endif
    // --version must exit before QApplication so no display is required
    // (release-versioning spec).
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (std::string_view(args[i]) == "--version") {
            std::cout << devmgr::core::versionLine("devmgr-gui") << "\n";
            return 0;
        }
    }
    return devmgr::gui::runGuiApp(argc, argv);
}
