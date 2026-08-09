#include "tui/src/views/legend.hpp"

#include <ftxui/screen/string.hpp>  // string_width

namespace devmgr::tui::views {

namespace {

// Display columns, not bytes: `s=create…` carries a three-byte ellipsis that
// occupies one column, and measuring it as three would abridge a legend that
// actually fits.
int columns(const std::string& s) {
    return static_cast<int>(ftxui::string_width(s));
}

std::string join(std::string_view viewName, const std::vector<std::string>& parts,
                 std::string_view sep) {
    std::string out = " ";
    out += viewName;
    out += " (";
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out += sep;
        out += parts[i];
    }
    out += ")";
    return out;
}

bool fits(const std::string& candidate, int width) {
    return width <= 0 || columns(candidate) <= width;
}

}  // namespace

std::string fitLegend(std::string_view viewName, const std::vector<LegendEntry>& entries,
                      int width) {
    std::vector<std::string> full;
    std::vector<std::string> brief;
    full.reserve(entries.size());
    brief.reserve(entries.size());
    for (const auto& e : entries) {
        full.push_back(e.full);
        brief.push_back(e.brief);
    }

    // Steps 1–4: spend typography before spending shortcuts.
    for (const std::vector<std::string>* parts : {&full, &brief}) {
        for (std::string_view sep : {"  ", " "}) {
            std::string candidate = join(viewName, *parts, sep);
            if (fits(candidate, width)) return candidate;
        }
    }

    // Step 5: a terminal too narrow for every shortcut. Drop from the middle
    // outward and say so with `…`, keeping the first entry and the last two.
    std::vector<std::string> kept = brief;
    while (kept.size() > 3) {
        kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(kept.size() / 2));
        std::vector<std::string> shown = kept;
        shown.insert(shown.end() - 2, "…");
        std::string candidate = join(viewName, shown, " ");
        if (fits(candidate, width)) return candidate;
    }

    // Narrower than three shortcuts and a view name. Nothing legible is on
    // offer here; the minimum-size notice (views::belowMinimumSize) owns this
    // case, so return the shortest honest legend rather than pretend.
    return join(viewName, kept, " ");
}

}  // namespace devmgr::tui::views
