#pragma once
#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Persistent bottom status/prompt line (docs/DESIGN.md §3.2: one row, stable
// screen edge, reverse-video). Pure. The message text originates in the
// ViewModels; this only frames it. `role` carries the outcome severity mapped
// from StatusLineVM::severity() (nullopt = neutral prompt/steady state): the
// colour lands under the reverse video, so a success/danger outcome reads as a
// green/red bar while the text still says what happened (§10).
ftxui::Element renderStatusBar(const std::string& text, std::optional<Role> role,
                               const Theme& theme);

}  // namespace devmgr::tui::views
