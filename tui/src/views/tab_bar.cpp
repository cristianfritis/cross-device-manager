#include "tui/src/views/tab_bar.hpp"

#include <string>

namespace devmgr::tui::views {

ftxui::Element renderTabBar(int activeTab, const Theme& theme) {
    using namespace ftxui;
    // The digit hints stay plain so the bar reads as navigation, not decoration.
    // Letters collide with existing verbs (d/u/U), so digits are the direct-
    // access keys and 'm' cycles.
    //
    // R2: the active view is accent + bold in Full mode, and its bracket pair
    // changes from [n] to {n} so "which view am I in" survives with no colour at
    // all (Mono/Plain) — bold alone was too easy to miss against the bold legend
    // line directly beneath it. Braces are width-safe ASCII, so the bar's
    // geometry is identical either way, and they are deliberately none of the
    // other markers in play: the focus cursor ">", the criticality "#" and the
    // snapshot "*". Accent and never warning/yellow — yellow is the risk role
    // (docs/DESIGN.md §4.1) and "current mode" is not a risk (design Decision 6).
    auto name = [&](const char* key, const char* label, int tab) {
        const bool active = tab == activeTab;
        const std::string bracketed =
            active ? "{" + std::string(key) + "}" : "[" + std::string(key) + "]";
        Element e = hbox({text(bracketed), text(label)});
        return active ? e | theme.decorate(Role::Accent) | bold : e;
    };
    return hbox({text(" "), name("1", "Devices", 0), text(" | "), name("2", "Modules", 1),
                 text(" | "), name("3", "Updates", 2), text(" | "), name("4", "Snapshots", 3),
                 text("  (m: next tab) ")});
}

}  // namespace devmgr::tui::views
