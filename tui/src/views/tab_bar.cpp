#include "tui/src/views/tab_bar.hpp"

#include <string>

namespace devmgr::tui::views {

ftxui::Element renderTabBar(int activeTab, [[maybe_unused]] const Theme& theme) {
    using namespace ftxui;
    // Only the active view is bold; the digit hints stay plain so the bar reads
    // as navigation, not decoration. Letters collide with existing verbs
    // (d/u/U), so digits are the direct-access keys and 'm' cycles.
    auto name = [&](const char* key, const char* label, int tab) {
        Element e = hbox({text(std::string("[") + key + "]"), text(label)});
        return activeTab == tab ? e | bold : e;
    };
    return hbox({text(" "), name("1", "Devices", 0), text(" | "), name("2", "Modules", 1),
                 text(" | "), name("3", "Updates", 2), text(" | "), name("4", "Snapshots", 3),
                 text("  (m: next tab) ")});
}

}  // namespace devmgr::tui::views
