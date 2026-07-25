#include "tui/src/views/updates_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/render_util.hpp"  // render::hsep, render::regionFrame
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderUpdatesView(UpdatesView v, const Theme& theme) {
    using namespace ftxui;
    // Structure and decorators are the prior in-closure composition unchanged;
    // the request banner is still an optional bold row above the separator.
    // The diagnostics key is listed only while there is something to reveal
    // (single legend, no dead shortcuts advertised).
    const bool diagnosable = !v.diagnosticLines.empty();
    // The glyph precedes the sentence so a degraded backend is recognizable with
    // no colour at all (§10); the role is additive on top of it.
    std::string bannerLine = " ";
    if (v.bannerGlyph) bannerLine += std::string(render::glyph(*v.bannerGlyph, theme)) + " ";
    bannerLine += v.banner + " ";
    // elidedText, not text: the banner now carries a full sentence, and a string
    // wider than the terminal must elide visibly rather than be clipped mid-word.
    Element banner = render::elidedText(bannerLine, theme);
    if (v.bannerRole) banner = banner | theme.decorate(*v.bannerRole);
    Elements top = {
        renderTabBar(v.activeTab, theme),
        text(diagnosable ? " Updates (u=install  r=refresh  d=dismiss  i=diagnostics  q=quit) "
                         : " Updates (u=install  r=refresh  d=dismiss  q=quit) ") |
            bold,
        std::move(banner),
    };
    if (!v.requestBanner.empty()) top.push_back(text(" " + v.requestBanner + " ") | bold);
    top.push_back(render::hsep(theme));
    Elements listPane;
    if (!v.columnHeader.empty()) {
        listPane.push_back(text(" " + v.columnHeader) | theme.decorate(Role::Muted));
        listPane.push_back(render::hsep(theme));
    }
    listPane.push_back(std::move(v.list) | vscroll_indicator | yframe | flex);
    top.push_back(hbox({
                      render::regionFrame(
                          vbox(std::move(listPane)) | size(WIDTH, EQUAL, v.leftPaneWidth), theme),
                      render::regionFrame(std::move(v.detail), theme) | flex,
                  }) |
                  flex);
    // Diagnostics region (design D4): a muted header and a rule, NOT a box —
    // §4.3 keeps borders for major regions, and the raw detail is a subordinate
    // reveal under the banner it explains, not a region of its own rank. It sits
    // above the status bar so the status line keeps the bottom edge (§3.2).
    if (diagnosable && v.showDiagnostics) {
        top.push_back(render::hsep(theme));
        top.push_back(text(" -- Diagnostics -- ") | theme.decorate(Role::Muted));
        for (const auto& line : v.diagnosticLines)
            top.push_back(render::elidedText(" " + line, theme));
    }
    top.push_back(renderStatusBar(v.statusText, v.statusRole, theme));
    return vbox(std::move(top)) | flex;
}

}  // namespace devmgr::tui::views
