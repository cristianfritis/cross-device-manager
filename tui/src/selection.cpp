#include "tui/src/selection.hpp"

#include <algorithm>  // std::clamp
#include <array>

namespace devmgr::tui::nav {

// A row index, a row count and a direction are three distinct roles a caller
// cannot meaningfully confuse, and the direction only ever takes -1/0/+1:
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int snapToSelectable(int selected, int rowCount, int dir, const Selectable& selectable) {
    if (rowCount <= 0) return 0;
    const int start = std::clamp(selected, 0, rowCount - 1);
    if (selectable(start)) return start;
    // Continue the way the cursor was travelling first (0 == unknown reads as
    // "downwards", which is where a fresh list starts), then sweep back.
    const int forward = dir < 0 ? -1 : 1;
    for (const int step : std::array<int, 2>{forward, -forward}) {
        for (int i = start + step; i >= 0 && i < rowCount; i += step) {
            if (selectable(i)) return i;
        }
    }
    return start;  // no selectable row: the caller shows no cursor
}

bool anySelectable(int rowCount, const Selectable& selectable) {
    for (int i = 0; i < rowCount; ++i) {
        if (selectable(i)) return true;
    }
    return false;
}

}  // namespace devmgr::tui::nav
