#include "gui/src/gui_app.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <vector>

#include <QApplication>
#include <QCoreApplication>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "gui/src/main_window.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

namespace devmgr::gui {
namespace {
// Same helper as tui/src/tui_app.cpp's drainPending (deliberately duplicated:
// 5 lines; hoisting it into devmgr_app would be interface churn for nothing).
void drainPending(std::vector<std::future<void>>& pending) {
    for (auto& f : pending) {
        if (f.valid()) f.wait();
    }
}
}  // namespace

int runGuiApp(int argc, char** argv) {
    QApplication qapp(argc, argv);
    const bool selfTest = QCoreApplication::arguments().contains(QStringLiteral("--self-test"));

    // Declaration order below is the teardown contract (reverse-destruction):
    // hotplug/delayed MUST be declared before the dispatcher and VMs so they
    // are still alive when the VMs unwind — identical to runTuiApp().
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    runtime::DelayedScheduler delayed;
    platform_linux::UdevDeviceEnumerator enumerator;
    platform_linux::UdevHotplugMonitor monitor;
    app::DeviceService service(bus);
    app::ApplicationFacade facade(enumerator, scheduler, bus, service);
    app::HotplugService hotplug(monitor, service, delayed);  // 250 ms default window

    QtUiDispatcher dispatcher;
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);
    app::StatusLineVM statusVm(bus, delayed, dispatcher);

    // Keep every refresh future alive so we can wait on them before teardown —
    // ApplicationFacade::refresh()'s documented lifetime contract.
    std::vector<std::future<void>> pending;

    MainWindow window(listVm, detailVm, statusVm, dispatcher, [&] {
        // Drop already-completed refreshes so `pending` stays bounded.
        std::erase_if(pending, [](const std::future<void>& f) {
            return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
        pending.push_back(facade.refresh());
    });

    int rc = 0;
    // The try widens over hotplug.start()/publish for the same reason as
    // runTuiApp(): live threads exist before exec(), so an exception on any
    // pre-loop call must stop them before the VMs/dispatcher unwind.
    try {
        facade.refresh().wait();
        // The worker's queued rebuild is delivered here, before first paint —
        // the GUI-thread analogue of the TUI's dispatcher.drain().
        QCoreApplication::processEvents();
        statusVm.arm();
        if (auto started = hotplug.start(); !started) {
            // Degrade gracefully: without live events, Refresh still works.
            bus.publish(core::ErrorEvent{.source = "hotplug", .message = started.error().message});
        }
        if (selfTest) {
            std::cout << "self-test rows: " << listVm.rowsRef().size() << '\n';
        } else {
            window.show();
            rc = QApplication::exec();
        }
    } catch (...) {
        hotplug.stop();
        delayed.shutdown();
        drainPending(pending);
        throw;
    }

    // Stop event sources before the VMs/dispatcher unwind, then wait for any
    // in-flight refresh — order and rationale identical to runTuiApp().
    hotplug.stop();
    delayed.shutdown();
    drainPending(pending);
    return rc;
}

}  // namespace devmgr::gui
