#include "tui/src/views/status_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderStatusBar(const std::string& text, std::optional<Role> role,
                               const Theme& theme) {
    using namespace ftxui;
    // One padded, reverse-video row. The leading/trailing space keeps the text
    // off the screen edge. The role colour (identity outside Full mode) is
    // applied before the reverse video, so it renders as the bar's background.
    Element e = ftxui::text(" " + text + " ");
    if (role) e = e | theme.decorate(*role);
    return e | inverted;
}

}  // namespace devmgr::tui::views
