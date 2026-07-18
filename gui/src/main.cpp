#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>

#include "devmgr/core/version.hpp"
#include "gui/src/gui_app.hpp"

int main(int argc, char** argv) {
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
