#include "tui/src/views/modules_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderModulesView(ModulesView v, const Theme& theme) {
    using namespace ftxui;
    // Structure and decorators are the prior in-closure composition unchanged.
    return vbox({
               renderTabBar(v.activeTab, theme),
               text(" Modules (/=filter  l=load  u=unload  q=quit) ") | bold,
               text(" " + v.banner + " "),
               separator(),
               hbox({
                   vbox({
                       std::move(v.filterInput),
                       separator(),
                       std::move(v.list) | vscroll_indicator | yframe | flex,
                   }) | size(WIDTH, EQUAL, v.leftPaneWidth) |
                       border,
                   std::move(v.detail) | border | flex,
               }) | flex,
               renderStatusBar(v.statusText, theme),
           }) |
           flex;
}

}  // namespace devmgr::tui::views
