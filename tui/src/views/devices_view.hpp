#pragma once
#include <string>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Selection/focus marker policy for a single Devices-list row (docs/DESIGN.md
// §2.3 master-detail, §4.5 selection emphasis). Extracted verbatim from the
// menu-entry transform so it is unit-testable: active → "> " prefix + bold,
// focused → reverse video, otherwise a two-space alignment prefix. Pure — the
// marquee windowing stays in the shell (it needs live tick state) and hands the
// already-windowed label in. Semantic status colour arrives in group 4.
ftxui::Element renderDeviceRow(const std::string& label, bool active, bool focused);

// Complete Devices tab composition: navigation bar, bold legend, master-detail
// split (filter + scrolling device list on the left, detail on the right) and a
// reverse-video status line (docs/DESIGN.md §2.3, §3.2, §9). Pure framing over
// the shell's pre-rendered interactive bodies (filter Input, device Menu,
// detail Renderer) — the interactive components keep their behaviour while the
// layout renders identically against a fixed Screen for tests. No colour yet;
// borders/separators are unchanged from the prior build (theme-aware borders and
// semantic colour land in group 4).
struct DevicesView {
    int activeTab;
    ftxui::Element filterInput;  // searchInput->Render()
    ftxui::Element deviceList;   // deviceMenu->Render() (raw; scroll-framed here)
    ftxui::Element detail;       // detailRenderer->Render()
    std::string statusText;
    int leftPaneWidth;
};
ftxui::Element renderDevicesView(DevicesView view, const Theme& theme);

}  // namespace devmgr::tui::views
