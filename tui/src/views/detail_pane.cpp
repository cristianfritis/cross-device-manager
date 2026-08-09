#include "tui/src/views/detail_pane.hpp"

#include <utility>  // std::move

#include "tui/src/render_util.hpp"  // render::elidedText

namespace devmgr::tui::views {

ftxui::Element renderDetailPane(const std::vector<std::string>& lines, const Theme& theme) {
    using namespace ftxui;
    Elements els;
    els.reserve(lines.size());
    for (const auto& line : lines) els.push_back(render::elidedText(line, theme));
    return vbox(std::move(els)) | flex;
}

}  // namespace devmgr::tui::views
