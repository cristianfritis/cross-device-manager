#pragma once
#include <optional>
#include <string>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/theme.hpp"

namespace devmgr::tui::views {

// Complete Modules tab composition: navigation bar, bold legend, Secure Boot /
// lockdown banner row, master-detail split (filter + scrolling module list on
// the left, detail on the right) and a reverse-video status line
// (docs/DESIGN.md §2.3, §3.2, §5.5, §9). Pure framing over the shell's
// pre-rendered interactive bodies (filter Input, module Menu, detail Renderer)
// so the interactive components keep their behaviour while the layout renders
// identically against a fixed Screen for tests. No colour yet; the banner is
// plain text and borders are unchanged from the prior build (semantic colour
// lands in group 4).
struct ModulesView {
    int activeTab;
    std::string banner;
    ftxui::Element filterInput;  // moduleFilterInput->Render()
    ftxui::Element list;         // modulesMenu->Render() (raw; scroll-framed here)
    ftxui::Element detail;       // moduleDetail->Render()
    std::string statusText;
    int leftPaneWidth;
    // Secure Boot / lockdown banner valence: Info in the steady state, Warning
    // when it explains a likely refusal (§5.5). nullopt leaves it uncoloured.
    std::optional<Role> bannerRole{};
    std::optional<Role> statusRole{};  // outcome severity for the status line (nullopt = neutral)
};
ftxui::Element renderModulesView(ModulesView view, const Theme& theme);

}  // namespace devmgr::tui::views
