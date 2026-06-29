#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

int main() {
    using namespace ftxui;
    auto screen = ScreenInteractive::Fullscreen();
    auto root = Renderer([] { return text("devmgr-tui (scaffold)") | border; });
    screen.Loop(root);
    return 0;
}
