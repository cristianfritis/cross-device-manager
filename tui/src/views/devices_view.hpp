#pragma once
#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/render_util.hpp"  // render::Glyph, render::menuRow
#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Selection/focus marker policy for a single Devices-list row (docs/DESIGN.md
// §2.3 master-detail, §4.5 selection emphasis, §4.1 semantic colour): active →
// "> " prefix + bold, focused → reverse video, otherwise a two-space alignment
// prefix. `statusGlyph`/`role` colour and glyph-mark the row by its device
// status (the shell maps DeviceStatus → glyph+role; header/placeholder rows pass
// nullopt). Pure — the marquee windowing stays in the shell (it needs live tick
// state) and hands the already-windowed label in. The default arguments preserve
// the earlier no-colour signature for callers/tests that do not colour.
ftxui::Element renderDeviceRow(const std::string& label, bool active, bool focused,
                               std::optional<render::Glyph> statusGlyph = std::nullopt,
                               std::optional<Role> role = std::nullopt,
                               const Theme& theme = Theme{});

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
    std::optional<Role> statusRole{};  // outcome severity for the status line (nullopt = neutral)
};
ftxui::Element renderDevicesView(DevicesView view, const Theme& theme);

}  // namespace devmgr::tui::views
