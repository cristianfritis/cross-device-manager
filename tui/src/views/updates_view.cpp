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
    Elements top = {
        renderTabBar(v.activeTab, theme),
        text(" Updates (u=install  r=refresh  d=dismiss  q=quit) ") | bold,
        text(" " + v.banner + " "),
    };
    if (!v.requestBanner.empty()) top.push_back(text(" " + v.requestBanner + " ") | bold);
    top.push_back(render::hsep(theme));
    top.push_back(hbox({
                      render::regionFrame(vbox({
                                              std::move(v.list) | vscroll_indicator | yframe | flex,
                                          }) | size(WIDTH, EQUAL, v.leftPaneWidth),
                                          theme),
                      render::regionFrame(std::move(v.detail), theme) | flex,
                  }) |
                  flex);
    top.push_back(renderStatusBar(v.statusText, v.statusRole, theme));
    return vbox(std::move(top)) | flex;
}

}  // namespace devmgr::tui::views
