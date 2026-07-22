#pragma once
#include <string>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Complete Updates tab composition: navigation bar, bold legend, availability
// banner, optional bold request banner, list + detail split and a reverse-video
// status line (docs/DESIGN.md §2.3, §3.2, §5.5, §9). Unlike the other lists the
// updates pane has no filter Input. Pure framing over the shell's pre-rendered
// interactive bodies so the interactive components keep their behaviour while
// the layout renders identically against a fixed Screen for tests. No colour
// yet; semantic colour lands in group 4.
struct UpdatesView {
    int activeTab;
    std::string banner;
    std::string requestBanner;  // empty → row omitted (updatesVm.requestBanner())
    ftxui::Element list;        // updatesMenu->Render() (raw; scroll-framed here)
    ftxui::Element detail;      // updatesDetail->Render()
    std::string statusText;
    int leftPaneWidth;
};
ftxui::Element renderUpdatesView(UpdatesView view, const Theme& theme);

}  // namespace devmgr::tui::views
