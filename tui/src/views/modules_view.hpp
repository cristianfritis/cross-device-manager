#pragma once
#include <optional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/semantics.hpp"  // render::Glyph
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
    // One muted, non-selectable column header from ModulesVM::columnHeader()
    // (R5). Rendered here rather than pushed into the Menu, so it is
    // structurally incapable of taking the cursor. Empty string = no header.
    std::string columnHeader;
    ftxui::Element list;    // modulesMenu->Render() (raw; scroll-framed here)
    ftxui::Element detail;  // moduleDetail->Render()
    std::string statusText;
    int leftPaneWidth;
    // Secure Boot / lockdown banner valence: Info in the steady state, Warning
    // when it explains a likely refusal (§5.5). nullopt leaves it uncoloured.
    std::optional<Role> bannerRole{};
    std::optional<Role> statusRole{};  // outcome severity for the status line (nullopt = neutral)
    // ---- Backend availability (backend-availability spec, §13) ----
    // The module LIST is read locally, but load/unload are devmgrd's. The
    // sentence itself is already folded into `banner` by ModulesVM::bannerLine()
    // (one banner row, one severity); what the view adds is the glyph and the
    // same `i` diagnostics region every other tab has.
    std::optional<render::Glyph> bannerGlyph{};
    std::vector<std::string> diagnosticLines;
    bool showDiagnostics = false;
    // Terminal width in columns, so the legend can be composed to FIT rather
    // than be silently clipped by the screen (§14 F3). 0 means "unknown", which
    // yields the roomiest legend — the behaviour before this field existed.
    int terminalWidth = 0;
};
ftxui::Element renderModulesView(ModulesView view, const Theme& theme);

}  // namespace devmgr::tui::views
