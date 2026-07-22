#include "tui/src/views/devices_view.hpp"

#include <string>
#include <utility>  // std::move

#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

ftxui::Element renderDeviceRow(const std::string& label, bool active, bool focused) {
    using namespace ftxui;
    // Byte-for-byte the prior menu-entry transform: alignment prefix, then
    // reverse video for keyboard focus and bold for the active selection.
    Element e = text((active ? "> " : "  ") + label);
    if (focused) e = e | inverted;
    if (active) e = e | bold;
    return e;
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
               renderStatusBar(v.statusText, theme),
           }) |
           flex;
}

}  // namespace devmgr::tui::views
