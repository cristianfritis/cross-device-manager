#pragma once
#include <string>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Persistent bottom status/prompt line (docs/DESIGN.md §3.2: one row, stable
// screen edge, reverse-video). Pure. The message text originates in the
// ViewModels; this only frames it. Outcome-severity colour arrives in group 4.
ftxui::Element renderStatusBar(const std::string& text, const Theme& theme);

}  // namespace devmgr::tui::views
