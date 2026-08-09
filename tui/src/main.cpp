#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

#include "devmgr/core/version.hpp"
#include "tui/src/theme.hpp"
#include "tui/src/tui_app.hpp"

int main(int argc, char** argv) {
    // --version must exit before the alternate screen or any wiring is touched
    // (release-versioning spec).
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    bool selfTest = false;
    bool noColor = false;  // --no-color: disable colour, keep box-drawing (mono)
    bool ascii = false;    // --ascii: ASCII-only, no colour and no box-drawing (plain)
    bool unicode = false;  // --unicode: opt into Unicode status glyphs (Full only)
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg(args[i]);
        if (arg == "--version") {
            std::cout << devmgr::core::versionLine("devmgr-tui") << "\n";
            return 0;
        }
        if (arg == "--self-test") selfTest = true;
        if (arg == "--no-color") noColor = true;
        if (arg == "--ascii") ascii = true;
        if (arg == "--unicode") unicode = true;
    }
    // NO_COLOR / TERM=dumb in the environment also feed the resolution.
    const devmgr::tui::Theme theme = devmgr::tui::Theme::resolve(noColor, ascii, unicode);
    return devmgr::tui::runTuiApp(selfTest, theme);
}
