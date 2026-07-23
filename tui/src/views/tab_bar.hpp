#pragma once
#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Primary navigation bar (docs/DESIGN.md §9): names all four views with their
// direct-access digit. The active view is accent + bold, and its bracket pair
// changes from [n] to {n} so it stays unmistakable with no colour at all (R2).
// Never the warning/yellow role — "current mode" is not a risk (§4.1). Pure:
// depends only on the active tab index and the theme.
ftxui::Element renderTabBar(int activeTab, const Theme& theme);

}  // namespace devmgr::tui::views
