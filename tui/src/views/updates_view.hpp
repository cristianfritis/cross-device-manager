#pragma once
#include <optional>
#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>  // Element

#include "tui/src/semantics.hpp"  // render::Glyph
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
    // One muted, non-selectable column header from UpdatesVM::columnHeader()
    // (R5). Rendered here rather than pushed into the Menu, so it is
    // structurally incapable of taking the cursor. Empty string = no header.
    std::string columnHeader;
    ftxui::Element list;    // updatesMenu->Render() (raw; scroll-framed here)
    ftxui::Element detail;  // updatesDetail->Render()
    std::string statusText;
    int leftPaneWidth;
    std::optional<Role> statusRole{};  // outcome severity for the status line (nullopt = neutral)
    // ---- Backend availability (backend-availability spec) ----
    // The banner's role and glyph arrive from the ViewModel rather than being
    // parsed back out of its text, so rewording a sentence cannot recolour it.
    // The glyph is the documented "unavailable" one, which is what carries the
    // state in Mono/Plain where the role decorator is identity.
    std::optional<Role> bannerRole{};
    std::optional<render::Glyph> bannerGlyph{};
    // Raw diagnostics, revealed by `i` (app::diagnosticLines()). Empty ⇒ every
    // backend is serving, so the key is inert and stays out of the legend.
    std::vector<std::string> diagnosticLines;
    bool showDiagnostics = false;
    // Terminal width in columns, so the legend can be composed to FIT rather
    // than be silently clipped by the screen (§14 F3). 0 means "unknown", which
    // yields the roomiest legend — the behaviour before this field existed.
    int terminalWidth = 0;
};
ftxui::Element renderUpdatesView(UpdatesView view, const Theme& theme);

}  // namespace devmgr::tui::views
