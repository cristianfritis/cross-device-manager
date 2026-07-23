#include "tui/src/views/snapshots_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/render_util.hpp"  // render::hsep, render::regionFrame
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderSnapshotsView(SnapshotsView v, const Theme& theme) {
    using namespace ftxui;
    // Structure and decorators are the prior in-closure composition unchanged.
    Elements top = {
        renderTabBar(v.activeTab, theme),
        text(" Snapshots (/=filter  s=create…  r=restore  d=diff  h=history  x=delete  "
             "q=quit) ") |
            bold,
        text(" " + v.banner + " "),
    };
    top.push_back(render::hsep(theme));
    if (v.showPreview) {
        // Modal body: the preview owns the pane while it is open, so the list
        // underneath cannot be mistaken for something the confirmation applies to.
        Elements lines;
        lines.reserve(v.previewLines.size());
        for (const auto& line : v.previewLines) lines.push_back(text(line));
        top.push_back(render::regionFrame(vbox(std::move(lines)), theme) | flex);
    } else {
        top.push_back(
            hbox({
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
        if (!v.guidanceLines.empty()) {
            Elements g;
            g.reserve(v.guidanceLines.size());
            for (const auto& line : v.guidanceLines) g.push_back(text(line));
            top.push_back(render::regionFrame(vbox(std::move(g)), theme));
        }
    }
    top.push_back(renderStatusBar(v.statusText, v.statusRole, theme));
    return vbox(std::move(top)) | flex;
}

}  // namespace devmgr::tui::views
