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
    // The direct-access digit is always tab+1, so it is derived rather than
    // passed — that also leaves the lambda with one `const char*` and one `int`
    // (not two adjacent same-typed params a caller could transpose).
    auto name = [&](const char* label, int tab) {
        const bool active = tab == activeTab;
        const std::string key = std::to_string(tab + 1);
        const std::string bracketed = active ? "{" + key + "}" : "[" + key + "]";
        Element e = hbox({text(bracketed), text(label)});
        return active ? e | theme.decorate(Role::Accent) | bold : e;
    };
    return hbox({text(" "), name("Devices", 0), text(" | "), name("Modules", 1), text(" | "),
                 name("Updates", 2), text(" | "), name("Snapshots", 3), text("  (m: next tab) ")});
}

}  // namespace devmgr::tui::views
