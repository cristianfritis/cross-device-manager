#pragma once
#include <cstdint>

#include <ftxui/component/event.hpp>  // ftxui::Event

namespace devmgr::tui::nav {

// What the shell does with a key while the active tab's filter Input owns focus
// (K4). The filter Input is a focused text field, so every printable key,
// backspace, arrow and mouse event belongs to IT — never to a single-key
// command. That is the no-leak guarantee: typing 'U' while searching cannot
// unbind a driver, and 'u'/'U'/digits/'/' are literal characters. Only Return
// (hand focus back, keep the text) and Escape (clear + hand back) are lifted out
// of the Input; nothing at all falls through to the command handlers.
enum class FilterKeyAction : std::uint8_t {
    NotFiltering,      // the filter is not focused → the shell proceeds to commands
    PassToInput,       // the Input consumes it (character/backspace/arrow/mouse)
    HandBackToList,    // Return → focus the list, keep the filter text
    ClearAndHandBack,  // Escape → clear the filter and focus the list
};

// Pure decision: no FTXUI focus mutation, no ViewModel access, so a test can
// drive the whole command-key union off-screen and prove none of it reaches a
// command while filtering. The shell passes `filterInput->Focused()` for the
// first argument and performs the side effect the returned action names.
inline FilterKeyAction routeFilterKey(bool filterFocused, const ftxui::Event& event) {
    if (!filterFocused) return FilterKeyAction::NotFiltering;
    if (event == ftxui::Event::Return) return FilterKeyAction::HandBackToList;
    if (event == ftxui::Event::Escape) return FilterKeyAction::ClearAndHandBack;
    return FilterKeyAction::PassToInput;
}

}  // namespace devmgr::tui::nav
