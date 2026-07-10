#include "gui/src/gui_app.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QCoreApplication>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#include "devmgr/platform/linux/kmod_driver_manager.hpp"
#include "devmgr/platform/linux/linux_system_info.hpp"
#ifdef DEVMGR_HAS_SDBUS
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"
#endif
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

// Deliberately one function: the declaration order below IS the teardown
// contract, and the line-for-line mirror with runTuiApp() is the point.
// NOLINTNEXTLINE(readability-function-size)
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
    platform_linux::LinuxCriticalityProber prober;  // advisory guard facts
    platform_linux::KmodDriverManager kmod;         // system defaults: /sys, real modules
    platform_linux::LinuxSystemInfo sysinfo;
#ifdef DEVMGR_HAS_SDBUS
    platform_linux::DbusPrivilegedChannel channel;  // system bus → devmgrd
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, &channel, &prober, &kmod,
                                  &sysinfo);
#else
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, nullptr, &prober, &kmod,
                                  &sysinfo);
#endif
    app::HotplugService hotplug(monitor, service, delayed);  // 250 ms default window

    QtUiDispatcher dispatcher;
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);
    app::StatusLineVM statusVm(bus, delayed, dispatcher);
    app::ModulesVM modulesVm(facade, bus, scheduler, dispatcher);

    // Keep every refresh future alive so we can wait on them before teardown —
    // ApplicationFacade::refresh()'s documented lifetime contract.
    std::vector<std::future<void>> pending;

    // Drop already-completed refreshes so `pending` stays bounded, then keep the
    // new future. Declared before both the subscription and the window because
    // each hands its future here.
    auto pruneAndPush = [&](std::future<void> f) {
        std::erase_if(pending, [](const std::future<void>& g) {
            return g.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
        pending.push_back(std::move(f));
    };

    // After a successful mutation, refresh so DeviceStatus mirrors sysfs again.
    // The handler runs on a scheduler worker; `pending` is GUI-thread state, so
    // hop through the dispatcher (delivered on the GUI thread) — same rationale
    // as tui/src/tui_app.cpp's refreshOnTaskDone.
    auto refreshOnTaskDone =
        bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
            if (!e.ok) return;
            dispatcher.post([&] { pruneAndPush(facade.refresh()); });
        });

    // Every mutation callback keeps future custody here (prune + push), the
    // same contract the TUI's key handlers honor; confirm/textInput stay empty
    // so MainWindow falls back to the real QMessageBox / QInputDialog.
    MainWindow::Actions actions;
    actions.onRefresh = [&] { pruneAndPush(facade.refresh()); };
    actions.onSetEnabled = [&](const core::DeviceId& id, bool enable) {
        pruneAndPush(facade.setDeviceEnabled(id, enable));
    };
    actions.onLoadModule = [&](const std::string& name) { pruneAndPush(facade.loadModule(name)); };
    actions.onUnloadModule = [&](const std::string& name) {
        pruneAndPush(facade.unloadModule(name));
    };
    actions.onBindDriver = [&](const core::DeviceId& id, const std::string& driver) {
        pruneAndPush(facade.bindDriver(id, driver));
    };
    actions.onUnbindDriver = [&](const core::DeviceId& id) {
        pruneAndPush(facade.unbindDriver(id));
    };
    MainWindow window(facade, listVm, detailVm, statusVm, modulesVm, dispatcher,
                      std::move(actions));

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
