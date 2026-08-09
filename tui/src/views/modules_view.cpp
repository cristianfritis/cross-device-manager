#include "tui/src/views/modules_view.hpp"

#include <string>
#include <utility>  // std::move
#include <vector>

#include "tui/src/render_util.hpp"  // render::hsep, render::regionFrame
#include "tui/src/views/legend.hpp"
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

namespace {
// The diagnostics key is listed only while there is something to reveal (single
// legend, no dead shortcuts advertised); `fitLegend` decides how much of the
// list the terminal can actually hold.
std::vector<LegendEntry> modulesLegend(bool diagnosable) {
    std::vector<LegendEntry> keys{"/=filter", "l=load", "u=unload"};
    if (diagnosable) keys.emplace_back("i=diagnostics", "i=diag");
    keys.emplace_back("q=quit");
    return keys;
}
}  // namespace

ftxui::Element renderModulesView(ModulesView v, const Theme& theme) {
    using namespace ftxui;
    // Structure and decorators are the prior composition; the banner now takes a
    // semantic role (info in steady state, warning when a refusal is likely).
    const bool diagnosable = !v.diagnosticLines.empty();
    // The glyph precedes the sentence so a degraded backend is recognizable with
    // no colour at all (§10); elidedText because a degraded banner carries the
    // shared sentence ahead of the posture and can outrun a narrow terminal.
    std::string bannerLine = " ";
    if (v.bannerGlyph) bannerLine += std::string(render::glyph(*v.bannerGlyph, theme)) + " ";
    bannerLine += v.banner + " ";
    Element banner = render::elidedText(bannerLine, theme);
    if (v.bannerRole) banner = banner | theme.decorate(*v.bannerRole);
    // Filter, then the column header, then the rows. The header is a plain muted
    // row in the pane — never a Menu entry — so it cannot be selected (R5).
    Elements listPane{std::move(v.filterInput), render::hsep(theme)};
    if (!v.columnHeader.empty()) {
        listPane.push_back(text(" " + v.columnHeader) | theme.decorate(Role::Muted));
    }
    listPane.push_back(std::move(v.list) | vscroll_indicator | yframe | flex);
    Elements top = {
        renderTabBar(v.activeTab, theme),
        text(fitLegend("Modules", modulesLegend(diagnosable), v.terminalWidth)) | bold,
        std::move(banner),
        render::hsep(theme),
        hbox({
            render::regionFrame(vbox(std::move(listPane)) | size(WIDTH, EQUAL, v.leftPaneWidth),
                                theme),
            render::regionFrame(std::move(v.detail), theme) | flex,
        }) | flex,
    };
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
