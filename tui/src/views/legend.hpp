#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devmgr::tui::views {

// One shortcut in a view's legend. `brief` is the form used once the roomy
// spelling stops fitting; it defaults to `full`, so an entry with no shorter
// spelling simply never shrinks.
struct LegendEntry {
    std::string full;
    std::string brief;

    // NOLINTNEXTLINE(google-explicit-constructor) — the call sites read as a
    // list of shortcut strings; requiring LegendEntry{"q=quit"} at each one buys
    // nothing. Taking `const char*` rather than `std::string` keeps a braced
    // list of literals to ONE user-defined conversion, which is all the language
    // allows implicitly.
    LegendEntry(const char* text) : full(text), brief(full) {}
    LegendEntry(std::string text, std::string shortText)
        : full(std::move(text)), brief(std::move(shortText)) {}
};

// Composes a view's single shortcut legend so that it FITS `width` display
// columns instead of being clipped by the terminal.
//
// Why this exists: the legend used to be a hardcoded string per view. Adding
// `i=diagnostics` (calm-backend-unavailability task 6.3) pushed Devices from 74
// to 89 columns and Snapshots from 81 to 96, so at the 80x24 minimum size the
// key was cut mid-word — `… x=delete  i=diagn` — and `q=quit` fell off the
// screen entirely. The clip is silent: FTXUI renders into a fixed-width screen,
// so nothing reports an overflow, and the render tests could not see it either
// (§14 F3/F5).
//
// Degradation order, roomiest first — each step gives up typography, never a
// shortcut, until there is no typography left to give:
//   1. two-space separators, full spellings   (the design's normal rhythm)
//   2. one-space separators, full spellings
//   3. two-space separators, brief spellings
//   4. one-space separators, brief spellings
//   5. drop entries from the middle, marking the gap with `…`
//
// The FIRST and LAST TWO entries are never dropped: the last is `q=quit` (how
// the user leaves — a legend that loses it is worse than one that admits it is
// abridged) and the one before it is the context-specific key the view is
// currently advertising, which while degraded is `i=diagnostics`.
//
// `width <= 0` means "unknown or unbounded" and yields the roomiest form, so a
// caller that does not know the terminal size behaves exactly as before.
std::string fitLegend(std::string_view viewName, const std::vector<LegendEntry>& entries,
                      int width);

}  // namespace devmgr::tui::views
