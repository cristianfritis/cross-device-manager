#include "tui/src/render_util.hpp"

#include <algorithm>  // std::max
#include <memory>     // std::make_shared
#include <string>
#include <string_view>
#include <utility>  // std::move
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>  // Utf8ToGlyphs

namespace devmgr::tui::render {
namespace {

// Control characters would either start a new row (`\n`) or render as an
// unpredictable width (`\t`); a status message is one row of prose, so they are
// flattened before the string is ever measured.
std::string flattenControlChars(std::string s) {
    for (char& c : s) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    return s;
}

class ElidedText : public ftxui::Node {
   public:
    ElidedText(std::vector<std::string> glyphs, std::vector<std::string> ellipsis)
        : glyphs_(std::move(glyphs)), ellipsis_(std::move(ellipsis)) {}

    void ComputeRequirement() override {
        // One row, and shrinkable to a single column: the point of this node is
        // that the layout decides the width and the text adapts, rather than the
        // text demanding a width the screen may not have.
        requirement_.min_x = 1;
        requirement_.min_y = 1;
    }

    void Render(ftxui::Screen& screen) override {
        const int y = box_.y_min;
        if (y > box_.y_max) return;
        const int width = box_.x_max - box_.x_min + 1;
        if (width <= 0) return;

        const int glyphCount = static_cast<int>(glyphs_.size());
        int x = box_.x_min;
        if (glyphCount <= width) {
            for (const auto& g : glyphs_) {
                screen.PixelAt(x, y).character = g;
                ++x;
            }
            return;
        }
        // Too wide: keep the head and mark the truncation, so a clipped message
        // reads as clipped instead of as a sentence that stops mid-word.
        const int ellipsisWidth = static_cast<int>(ellipsis_.size());
        const int head = std::max(0, width - ellipsisWidth);
        for (int i = 0; i < head; ++i) {
            screen.PixelAt(x, y).character = glyphs_[static_cast<std::size_t>(i)];
            ++x;
        }
        for (const auto& g : ellipsis_) {
            if (x > box_.x_max) break;
            screen.PixelAt(x, y).character = g;
            ++x;
        }
    }

   private:
    std::vector<std::string> glyphs_;
    std::vector<std::string> ellipsis_;
};

}  // namespace

int revealMaxOffset(std::string_view s, int width) {
    const int n = static_cast<int>(ftxui::Utf8ToGlyphs(std::string(s)).size());
    return std::max(0, n - std::max(width, 0));
}

std::string revealWindow(std::string_view s, int width, int offset) {
    if (width <= 0) return {};
    const auto glyphs = ftxui::Utf8ToGlyphs(std::string(s));
    const int n = static_cast<int>(glyphs.size());
    if (n <= width) return std::string(s);
    const int at = std::clamp(offset, 0, n - width);
    std::string out;
    for (int i = at; i < at + width; ++i) out += glyphs[static_cast<std::size_t>(i)];
    return out;
}

int revealOffset(int tick, int maxOffset) {
    // ~0.6 s at the 150 ms tick rate: long enough to read the head of the name
    // before it starts moving.
    constexpr int kLeadInTicks = 4;
    return std::clamp(tick - kLeadInTicks, 0, std::max(maxOffset, 0));
}

ftxui::Element elidedText(std::string text, const Theme& theme) {
    // Plain mode emits no non-ASCII byte, borders and ellipsis alike.
    const char* ellipsis = theme.asciiBorders() ? "..." : "…";
    return std::make_shared<ElidedText>(ftxui::Utf8ToGlyphs(flattenControlChars(std::move(text))),
                                        ftxui::Utf8ToGlyphs(ellipsis));
}

}  // namespace devmgr::tui::render
