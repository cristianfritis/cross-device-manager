#include "tui/src/views/devices_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderDeviceRow(const std::string& label, bool selected, bool listFocused,
                               std::optional<render::Glyph> statusGlyph, std::optional<Role> role,
                               const Theme& theme, std::optional<render::Badge> badge) {
    // Devices share the one selection treatment every list uses; render::menuRow
    // layers the status glyph and role colour on top (both nullopt here reproduce
    // the plain row byte-for-byte).
    return render::menuRow(label, selected, listFocused, statusGlyph, role, theme, badge);
}

ftxui::Element renderDevicesView(DevicesView v, const Theme& theme) {
    using namespace ftxui;
    // Structure and decorators are the prior in-closure composition unchanged;
    // only the status line is now framed via views::renderStatusBar (identical
    // reverse-video output) so the theme is consumed here too.
    return vbox({
               renderTabBar(v.activeTab, theme),
               text(" Devices (/=filter  r=refresh  e=enable/disable  U=unbind  B=bind  "
                    "q=quit) ") |
                   bold,
               render::hsep(theme),
               hbox({
                   render::regionFrame(
                       vbox({
                           std::move(v.filterInput),
                           render::hsep(theme),
                           std::move(v.deviceList) | vscroll_indicator | yframe | flex,
                       }) | size(WIDTH, EQUAL, v.leftPaneWidth),
                       theme),
                   render::regionFrame(std::move(v.detail), theme) | flex,
               }) | flex,
               renderStatusBar(v.statusText, v.statusRole, theme),
           }) |
           flex;
}

}  // namespace devmgr::tui::views
