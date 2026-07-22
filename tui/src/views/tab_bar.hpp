#pragma once
#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Primary navigation bar (docs/DESIGN.md §9): names all four views with their
// direct-access digit; the active view is emphasised. Pure — depends only on
// the active tab index and the theme. Semantic accent colour is layered on in
// group 4; this extraction preserves the prior bold-only appearance.
ftxui::Element renderTabBar(int activeTab, const Theme& theme);

}  // namespace devmgr::tui::views
