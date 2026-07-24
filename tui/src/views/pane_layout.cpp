#include "tui/src/views/pane_layout.hpp"

#include <algorithm>  // std::clamp

namespace devmgr::tui::views {
namespace {

// What the wide views ask for when the terminal can afford it (the pass-1/2
// constant).
constexpr int kPreferredWideLeftPane = 72;
// The list pane's own frame, which sits between the two panes' contents.
constexpr int kSplitBorders = 2;
// Floor for the detail pane, borders included: below this it cannot render a
// `Risk:` line, a sysfs path or a signer without eliding it to nothing.
constexpr int kMinDetailPane = 34;
// Floor for the list pane — the Devices pane width, so 80 columns lands on the
// 44/34 split that tab already ships.
constexpr int kMinWideLeftPane = 44;

}  // namespace

int wideLeftPaneWidth(int cols) {
    return std::clamp(cols - kSplitBorders - kMinDetailPane, kMinWideLeftPane,
                      kPreferredWideLeftPane);
}

}  // namespace devmgr::tui::views
