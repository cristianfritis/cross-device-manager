#include "tui/src/tui_app.hpp"

#include <future>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "tui/src/ftxui_ui_dispatcher.hpp"

namespace devmgr::tui {

int runTuiApp() {
    using namespace ftxui;

    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    platform_linux::UdevDeviceEnumerator enumerator;
    app::DeviceService service(bus);
    app::ApplicationFacade facade(enumerator, scheduler, bus, service);

    auto screen = ScreenInteractive::Fullscreen();
    FtxuiUiDispatcher dispatcher(screen);
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);

    // Keep every refresh future alive so we can wait on them before teardown
    // (see the note after screen.Loop()).
    std::vector<std::future<void>> pending;

    static constexpr int kLeftPaneWidth = 44;

    std::string filter;
    InputOption inputOpt;
    inputOpt.content = &filter;
    inputOpt.placeholder = "filter devices…";
    inputOpt.on_change = [&] { listVm.setFilter(filter); };
    auto searchInput = Input(inputOpt);

    auto deviceMenu = Menu(&listVm.rowsRef(), &listVm.selectedRef(), MenuOption::Vertical());

    auto leftPane = Container::Vertical({searchInput, deviceMenu});

    auto detailRenderer = Renderer([&] {
        Elements els;
        for (const auto& line : detailVm.lines(listVm.selectedDeviceId())) {
            els.push_back(text(line));
        }
        return vbox(std::move(els)) | flex;
    });

    auto layout = Container::Horizontal({leftPane, detailRenderer});
    auto ui = Renderer(layout, [&] {
        return hbox({
                   vbox({
                       text(" Devices (r=refresh  q=quit) ") | bold,
                       separator(),
                       searchInput->Render(),
                       separator(),
                       deviceMenu->Render() | vscroll_indicator | yframe | flex,
                   }) | size(WIDTH, EQUAL, kLeftPaneWidth) |
                       border,
                   detailRenderer->Render() | border | flex,
               }) |
               flex;
    });

    auto root = CatchEvent(ui, [&](const Event& event) {
        if (event == Event::Custom) {  // worker posted a UI update
            dispatcher.drain();
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
        if (event == Event::Character('r')) {
            pending.push_back(facade.refresh());  // fire; results arrive via the dispatcher
            return true;
        }
        return false;  // let Input / Menu handle the rest (incl. mouse)
    });

    pending.push_back(facade.refresh());  // initial populate without pressing 'r'
    screen.Loop(root);

    // Teardown safety: the VMs' EventBus handlers capture `this`. An enumeration still
    // running on the scheduler would publish into a half-destroyed VM as these locals
    // unwind (the scheduler is destroyed AFTER the VMs in stack order, so its drain/join
    // happens too late). Wait for every submitted refresh to finish here — by the time a
    // refresh's future is ready, its synchronous publish→onModelChanged→dispatcher.post
    // chain has completed while the VMs are still alive. This also honors
    // ApplicationFacade::refresh()'s documented lifetime contract (the future MUST be
    // awaited before the facade is destroyed).
    for (auto& f : pending) {
        if (f.valid()) f.wait();
    }
    return 0;
}

}  // namespace devmgr::tui
