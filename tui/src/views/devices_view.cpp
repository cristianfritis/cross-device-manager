#include "tui/src/views/devices_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderDeviceRow(const std::string& label, bool active, bool focused,
                               std::optional<render::Glyph> statusGlyph, std::optional<Role> role,
                               const Theme& theme) {
    // The alignment prefix, reverse-video focus and bold selection are the prior
    // menu-entry transform verbatim; render::menuRow layers the status glyph and
    // role colour on top (both nullopt here reproduce the old row byte-for-byte).
    return render::menuRow(label, active, focused, statusGlyph, role, theme);
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
               separator(),
               hbox({
                   vbox({
                       std::move(v.filterInput),
                       separator(),
                       std::move(v.deviceList) | vscroll_indicator | yframe | flex,
                   }) | size(WIDTH, EQUAL, v.leftPaneWidth) |
                       border,
                   std::move(v.detail) | border | flex,
               }) | flex,
               renderStatusBar(v.statusText, v.statusRole, theme),
           }) |
           flex;
}

}  // namespace devmgr::tui::views
