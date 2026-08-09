#pragma once
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// The right-hand detail pane body, shared by all four tabs: one row per
// ViewModel line, each BOUNDED to that row (R1). A detail value can be
// arbitrarily long — a hardware ID, a native identifier, a signer name — and `ftxui::text`
// would let it run to the screen edge and stop mid-token with nothing to say it
// had been cut. Every line goes through render::elidedText instead, so at 80
// columns a long value ends in a visible ellipsis and the pane never grows a
// second row for one line.
//
// Pure: the caller supplies the already-computed lines (the shell caches them —
// detailLines() does disk I/O and must never run in Render(), DESIGN.md §8).
ftxui::Element renderDetailPane(const std::vector<std::string>& lines, const Theme& theme);

}  // namespace devmgr::tui::views
