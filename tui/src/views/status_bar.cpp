#include "tui/src/views/status_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderStatusBar(const std::string& text, [[maybe_unused]] const Theme& theme) {
    using namespace ftxui;
    // One padded, reverse-video row. The leading/trailing space keeps the text
    // off the screen edge, matching the prior inline rendering byte-for-byte.
    return ftxui::text(" " + text + " ") | inverted;
}

}  // namespace devmgr::tui::views
