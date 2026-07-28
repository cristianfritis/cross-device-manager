#include "tui/src/views/snapshots_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/render_util.hpp"  // render::hsep, render::regionFrame
#include "tui/src/views/legend.hpp"
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

namespace {

// The body is either the preview modal or the master-detail split, and the
// split may carry a guidance panel — three shapes that have nothing to say to
// each other, lifted out so the top-level renderer reads as the linear list of
// rows it actually is.
void appendBody(SnapshotsView& v, const Theme& theme, ftxui::Elements& top) {
    using namespace ftxui;
    if (v.showPreview) {
        // Modal body: the preview owns the pane while it is open, so the list
        // underneath cannot be mistaken for something the confirmation applies to.
        Elements lines;
        lines.reserve(v.previewLines.size());
        for (const auto& line : v.previewLines) lines.push_back(text(line));
        top.push_back(render::regionFrame(vbox(std::move(lines)), theme) | flex);
        return;
    }
    top.push_back(hbox({
                      render::regionFrame(vbox({
                                              std::move(v.filterInput),
                                              render::hsep(theme),
                                              std::move(v.list) | vscroll_indicator | yframe | flex,
                                          }) | size(WIDTH, EQUAL, v.leftPaneWidth),
                                          theme),
                      render::regionFrame(std::move(v.detail), theme) | flex,
                  }) |
                  flex);
    // Recovery guidance for the last restore that did not fully converge
    // (snapshot-ui spec): durable, not a transient status line — it carries
    // the safety id and the exact command back.
    if (v.guidanceLines.empty()) return;
    Elements g;
    g.reserve(v.guidanceLines.size());
    for (const auto& line : v.guidanceLines) g.push_back(text(line));
    top.push_back(render::regionFrame(vbox(std::move(g)), theme));
}
// `s=create…` and `h=history` carry shorter spellings because this is the
// longest legend in the app: with the diagnostics key it wants 96 columns, and
// the minimum supported terminal is 80.
std::vector<LegendEntry> snapshotsLegend(bool diagnosable) {
    std::vector<LegendEntry> keys{"/=filter", {"s=create…", "s=create"}, "r=restore",
                                  "d=diff",   {"h=history", "h=hist"},   "x=delete"};
    if (diagnosable) keys.emplace_back("i=diagnostics", "i=diag");
    keys.emplace_back("q=quit");
    return keys;
}
}  // namespace

ftxui::Element renderSnapshotsView(SnapshotsView v, const Theme& theme) {
    using namespace ftxui;
    // Structure and decorators are the prior in-closure composition unchanged.
    // The diagnostics key is listed only while there is something to reveal
    // (single legend, no dead shortcuts advertised) — the Updates view's rule.
    const bool diagnosable = !v.diagnosticLines.empty();
    Elements top = {
        renderTabBar(v.activeTab, theme),
        text(fitLegend("Snapshots", snapshotsLegend(diagnosable), v.terminalWidth)) | bold,
    };
    // An empty banner is no banner: with no snapshots there are no counts to
    // summarise, and the list's "(no snapshots)" placeholder is the single empty
    // indicator (pass-2 bug B4). Skipping the row also returns it to the 80x24
    // budget rather than spending it on a blank line.
    if (!v.banner.empty()) {
        // The glyph precedes the sentence so a degraded daemon is recognizable
        // with no colour at all (§10); the role is additive on top of it.
        std::string bannerLine = " ";
        if (v.bannerGlyph) bannerLine += std::string(render::glyph(*v.bannerGlyph, theme)) + " ";
        bannerLine += v.banner + " ";
        // elidedText, not text: a degraded banner carries a full sentence, which
        // must elide visibly on a narrow terminal rather than clip mid-word.
        Element banner = render::elidedText(bannerLine, theme);
        if (v.bannerRole) banner = banner | theme.decorate(*v.bannerRole);
        top.push_back(std::move(banner));
    }
    top.push_back(render::hsep(theme));
    appendBody(v, theme, top);
    // Diagnostics region (design D4): a muted header and a rule, NOT a box, sat
    // above the status bar so the status line keeps the bottom edge (§3.2).
    // Byte-identical composition to the Updates view's region — the parity test
    // asserts both reveal the same lines.
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
