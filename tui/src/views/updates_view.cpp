#include "tui/src/views/updates_view.hpp"

#include <string>
#include <utility>  // std::move

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
    top.push_back(separator());
    top.push_back(hbox({
                      vbox({
                          std::move(v.list) | vscroll_indicator | yframe | flex,
                      }) | size(WIDTH, EQUAL, v.leftPaneWidth) |
                          border,
                      std::move(v.detail) | border | flex,
                  }) |
                  flex);
    top.push_back(renderStatusBar(v.statusText, v.statusRole, theme));
    return vbox(std::move(top)) | flex;
}

}  // namespace devmgr::tui::views
