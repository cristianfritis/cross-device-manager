#include "tui/src/views/devices_view.hpp"

#include <string>
#include <utility>  // std::move
#include <vector>

#include "tui/src/views/legend.hpp"
#include "tui/src/views/status_bar.hpp"
#include "tui/src/views/tab_bar.hpp"

namespace devmgr::tui::views {

namespace {
std::vector<LegendEntry> devicesLegend(bool diagnosable) {
    std::vector<LegendEntry> keys{"/=filter", "r=refresh", "e=enable/disable", "U=unbind",
                                  "B=bind"};
    if (diagnosable) keys.emplace_back("i=diagnostics", "i=diag");
    keys.emplace_back("q=quit");
    return keys;
}
}  // namespace

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
    // reverse-video output) so the theme is consumed here too. The availability
    // banner and the diagnostics region below are the Updates view's, verbatim —
    // same rows, same order, same legend rule.
    const bool diagnosable = !v.diagnosticLines.empty();
    Elements top = {
        renderTabBar(v.activeTab, theme),
        text(fitLegend("Devices", devicesLegend(diagnosable), v.terminalWidth)) | bold,
    };
    if (!v.banner.empty()) {
        std::string bannerLine = " ";
        if (v.bannerGlyph) bannerLine += std::string(render::glyph(*v.bannerGlyph, theme)) + " ";
        bannerLine += v.banner + " ";
        Element banner = render::elidedText(bannerLine, theme);
        if (v.bannerRole) banner = banner | theme.decorate(*v.bannerRole);
        top.push_back(std::move(banner));
    }
    top.push_back(render::hsep(theme));
    top.push_back(
        hbox({
            render::regionFrame(vbox({
                                    std::move(v.filterInput),
                                    render::hsep(theme),
                                    std::move(v.deviceList) | vscroll_indicator | yframe | flex,
                                }) | size(WIDTH, EQUAL, v.leftPaneWidth),
                                theme),
            render::regionFrame(std::move(v.detail), theme) | flex,
        }) |
        flex);
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
