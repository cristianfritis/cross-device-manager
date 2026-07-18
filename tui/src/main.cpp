#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

#include "devmgr/core/version.hpp"
#include "tui/src/tui_app.hpp"

int main(int argc, char** argv) {
    // --version must exit before the alternate screen or any wiring is touched
    // (release-versioning spec).
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    bool selfTest = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (std::string_view(args[i]) == "--version") {
            std::cout << devmgr::core::versionLine("devmgr-tui") << "\n";
            return 0;
        }
        if (std::string_view(args[i]) == "--self-test") selfTest = true;
    }
    return devmgr::tui::runTuiApp(selfTest);
}
