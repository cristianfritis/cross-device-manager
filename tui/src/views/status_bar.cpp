#include "tui/src/views/status_bar.hpp"

#include <string>

#include "tui/src/render_util.hpp"  // render::elidedText

namespace devmgr::tui::views {

ftxui::Element renderStatusBar(const std::string& text, std::optional<Role> role,
                               const Theme& theme) {
    using namespace ftxui;
    // One padded, reverse-video row. The leading/trailing space keeps the text
    // off the screen edge. The role colour (identity outside Full mode) is
    // applied before the reverse video, so it renders as the bar's background.
    //
    // render::elidedText, not ftxui::text: the bar is contractually ONE row
    // (docs/DESIGN.md §3.2) and a message it cannot fit must say so. Plain
    // `text` cuts a too-long message mid-word with nothing to mark the cut, so
    // a truncated refusal read as a sentence that simply stopped (pass-2 bug
    // B6); it also demands the full string width from the layout. The explicit
    // height clamp makes the one-row guarantee structural rather than a property
    // of whatever the message happens to contain.
    Element e = render::elidedText(" " + text + " ", theme);
    if (role) e = e | theme.decorate(*role);
    return e | inverted | size(HEIGHT, EQUAL, 1);
}

}  // namespace devmgr::tui::views
