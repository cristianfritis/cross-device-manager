#pragma once
#include <optional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Complete Snapshots tab composition (backup-rollback-engine, snapshot-ui
// spec): navigation bar, bold legend, banner, then either the restore-preview
// modal body or the master-detail split with an optional recovery-guidance
// panel, closed by a reverse-video status line (docs/DESIGN.md §2.3, §3.2, §9).
// When `showPreview` is set the modal owns the pane and the interactive bodies
// are unused (the shell leaves them null so their components are not rendered
// off-screen). Pure framing; no colour yet — semantic colour lands in group 4.
struct SnapshotsView {
    int activeTab;
    std::string banner;
    std::string statusText;
    int leftPaneWidth;
    bool showPreview = false;
    std::vector<std::string> previewLines;   // used when showPreview
    ftxui::Element filterInput;              // !showPreview: snapshotFilterInput->Render()
    ftxui::Element list;                     // !showPreview: snapshotsMenu->Render()
    ftxui::Element detail;                   // !showPreview: snapshotsDetail->Render()
    std::vector<std::string> guidanceLines;  // !showPreview: empty → panel omitted
    std::optional<Role> statusRole{};  // outcome severity for the status line (nullopt = neutral)
};
ftxui::Element renderSnapshotsView(SnapshotsView view, const Theme& theme);

}  // namespace devmgr::tui::views
