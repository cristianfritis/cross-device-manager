// K4 (task 12.1): while a filter Input owns focus, no command key may leak to a
// single-key command — typing 'U' mid-search must not unbind a driver. The shell
// funnels every key through nav::routeFilterKey while filtering, so proving that
// function never returns NotFiltering for any command key (i.e. never lets the
// event fall through to the command handlers) is the whole no-leak guarantee.
#include "tui/src/key_routing.hpp"

#include <array>
#include <string>

#include <ftxui/component/event.hpp>

#include <gtest/gtest.h>

namespace devmgr::tui {
namespace {

using nav::FilterKeyAction;
using nav::routeFilterKey;

// The full command-key union the legends bind across the four tabs, INCLUDING
// the case twins: the filter is case-insensitive, so 'u' (unload/install) and
// 'U' (unbind) are the same leak surface and both must type, not fire.
constexpr std::array<const char*, 13> kCommandKeys{
    {"r", "e", "U", "B", "q", "l", "u", "d", "s", "h", "x", "m", "/"}};

TEST(FilterRouting, EveryCommandKeyTypesIntoTheFilterAndNeverFires) {
    for (const char* k : kCommandKeys) {
        EXPECT_EQ(routeFilterKey(true, ftxui::Event::Character(std::string(k))),
                  FilterKeyAction::PassToInput)
            << "command key leaked while filtering: " << k;
    }
}

TEST(FilterRouting, DigitsTypeIntoTheFilter) {
    // The legend binds no digit handler, so 0-9 are ordinary filter text.
    for (char d = '0'; d <= '9'; ++d) {
        EXPECT_EQ(routeFilterKey(true, ftxui::Event::Character(std::string(1, d))),
                  FilterKeyAction::PassToInput)
            << "digit: " << d;
    }
}

TEST(FilterRouting, SlashWhileFilteringIsLiteralAndDoesNotReTrigger) {
    // '/' opens the filter only when NOT already filtering; once the Input has
    // focus it is a literal character, so it cannot re-trigger or loop.
    EXPECT_EQ(routeFilterKey(true, ftxui::Event::Character("/")), FilterKeyAction::PassToInput);
    EXPECT_EQ(routeFilterKey(false, ftxui::Event::Character("/")), FilterKeyAction::NotFiltering);
}

TEST(FilterRouting, EnterHandsFocusBackKeepingText_EscapeClears) {
    EXPECT_EQ(routeFilterKey(true, ftxui::Event::Return), FilterKeyAction::HandBackToList);
    EXPECT_EQ(routeFilterKey(true, ftxui::Event::Escape), FilterKeyAction::ClearAndHandBack);
}

TEST(FilterRouting, NavKeysReachTheInputNotCommands) {
    for (const ftxui::Event& nav : {ftxui::Event::ArrowUp, ftxui::Event::ArrowDown,
                                    ftxui::Event::Tab, ftxui::Event::TabReverse}) {
        EXPECT_EQ(routeFilterKey(true, nav), FilterKeyAction::PassToInput);
    }
}

// The complement: with the list focused, the very same keys report NotFiltering
// so the shell's single-key verbs stay live — the guarantee is scoped to the
// filter, it does not disable commands everywhere.
TEST(FilterRouting, WhenNotFilteringCommandKeysFallThroughToCommands) {
    for (const char* k : kCommandKeys) {
        EXPECT_EQ(routeFilterKey(false, ftxui::Event::Character(std::string(k))),
                  FilterKeyAction::NotFiltering)
            << "key: " << k;
    }
    // Return/Escape are also live commands (quit, dismiss) when not filtering.
    EXPECT_EQ(routeFilterKey(false, ftxui::Event::Return), FilterKeyAction::NotFiltering);
    EXPECT_EQ(routeFilterKey(false, ftxui::Event::Escape), FilterKeyAction::NotFiltering);
}

}  // namespace
}  // namespace devmgr::tui
