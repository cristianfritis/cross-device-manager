#include "tui/src/views/min_size.hpp"

#include <string>

namespace devmgr::tui::views {

bool belowMinimumSize(int cols, int rows) {
    return cols < kMinCols || rows < kMinRows;
}

ftxui::Element renderMinSizeNotice() {
    using namespace ftxui;
    const std::string minimum =
        "Minimum size is " + std::to_string(kMinCols) + "x" + std::to_string(kMinRows) + ".";
    return vbox({
               text("Terminal too small") | bold | center,
               text(minimum) | center,
               text("Resize the window, or press q to quit.") | center,
           }) |
           flex;
}

}  // namespace devmgr::tui::views
