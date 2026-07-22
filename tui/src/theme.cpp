#include "tui/src/theme.hpp"

#include <cstdlib>      // std::getenv
#include <string_view>  // std::string_view

namespace devmgr::tui {

ColorMode Theme::classify(bool noColor, bool ascii, bool termDumb) {
    if (ascii || termDumb) return ColorMode::Plain;  // strictest degrade wins
    if (noColor) return ColorMode::Mono;
    return ColorMode::Full;
}

Theme Theme::resolve(bool noColorFlag, bool asciiFlag, bool unicodeOptIn) {
    // NO_COLOR convention (no-color.org): present and non-empty disables colour.
    const char* noColorEnv = std::getenv("NO_COLOR");
    const bool noColorSet = noColorEnv != nullptr && !std::string_view(noColorEnv).empty();
    const char* term = std::getenv("TERM");
    const bool termDumb = term != nullptr && std::string_view(term) == "dumb";

    const ColorMode mode = classify(noColorFlag || noColorSet, asciiFlag, termDumb);
    // The constructor clamps the unicode opt-in to Full mode; the safe ASCII set
    // is used whenever colour is degraded.
    return {mode, unicodeOptIn};
}

ftxui::Decorator Theme::decorate(Role role) const {
    using namespace ftxui;
    // Identity decorator: leaves the element (and its pixel colours) untouched.
    if (mode_ != ColorMode::Full) return [](Element e) { return e; };
    switch (role) {
        case Role::Accent:
            return color(Color::Cyan);
        case Role::Success:
            return color(Color::Green);
        case Role::Warning:
            return color(Color::Yellow);
        case Role::Danger:
            return color(Color::Red);
        case Role::Info:
            return color(Color::Blue);
        case Role::Muted:
            return dim;  // secondary metadata: dim attribute, not a hue
    }
    return [](Element e) { return e; };
}

}  // namespace devmgr::tui
