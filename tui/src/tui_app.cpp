#include "tui/src/tui_app.hpp"

#include <chrono>
#include <future>
#include <optional>
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
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#ifdef DEVMGR_HAS_SDBUS
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"
#endif
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "tui/src/ftxui_ui_dispatcher.hpp"

namespace devmgr::tui {

namespace {
// Waits for every in-flight refresh future to complete. Shared by the
// exception-teardown path and the normal-path teardown so no scheduler-
// thread refresh can publish into a ViewModel that is mid-destruction.
void drainPending(std::vector<std::future<void>>& pending) {
    for (auto& f : pending) {
        if (f.valid()) f.wait();
    }
}
}  // namespace

int runTuiApp() {
    using namespace ftxui;

    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    runtime::DelayedScheduler delayed;
    platform_linux::UdevDeviceEnumerator enumerator;
    platform_linux::UdevHotplugMonitor monitor;
    app::DeviceService service(bus);
    platform_linux::LinuxCriticalityProber prober;  // advisory guard facts
#ifdef DEVMGR_HAS_SDBUS
    platform_linux::DbusPrivilegedChannel channel;  // system bus → devmgrd
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, &channel, &prober);
#else
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, nullptr, &prober);
#endif
    app::HotplugService hotplug(monitor, service, delayed);  // 250 ms default window

    auto screen = ScreenInteractive::Fullscreen();
    FtxuiUiDispatcher dispatcher(screen);
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);
    app::StatusLineVM statusVm(bus, delayed, dispatcher);

    // Keep every refresh future alive so we can wait on them before teardown
    // (see the note after screen.Loop()).
    std::vector<std::future<void>> pending;

    // Pending enable/disable confirmation ('e' arms it; y/n resolves it).
    struct PendingToggle {
        core::DeviceId id;
        bool enable;
        std::string prompt;
    };
    std::optional<PendingToggle> confirmToggle;

    auto prunePending = [&] {
        std::erase_if(pending, [](const std::future<void>& f) {
            return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
    };

    // After a successful mutation, refresh so DeviceStatus mirrors sysfs again.
    // The handler runs on a scheduler worker; `pending` is UI-thread state, so
    // hop through the dispatcher (drained on the UI thread via Event::Custom).
    auto refreshOnTaskDone =
        bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
            if (!e.ok) return;
            dispatcher.post([&] {
                prunePending();
                pending.push_back(facade.refresh());
            });
        });

    static constexpr int kLeftPaneWidth = 44;

    std::string filter;
    InputOption inputOpt;
    inputOpt.content = &filter;
    inputOpt.placeholder = "filter devices…";
    inputOpt.on_change = [&] { listVm.setFilter(filter); };
    auto searchInput = Input(inputOpt);

    auto deviceMenu = Menu(&listVm.rowsRef(), &listVm.selectedRef(), MenuOption::Vertical());

    auto leftPane = Container::Vertical({searchInput, deviceMenu});

    // Detail-pane render cache: FTXUI re-renders after every event (mouse moves
    // included), but the detail content can only change when the selection
    // moves or a model update arrives via Event::Custom. Rebuilding lines()
    // per frame copies the selected Device (properties map included) out of
    // the model every time — cache until selection or model changes instead.
    std::optional<core::DeviceId> detailForId;
    std::vector<std::string> detailLines;
    bool detailDirty = true;

    auto detailRenderer = Renderer([&] {
        const auto id = listVm.selectedDeviceId();
        if (detailDirty || id != detailForId) {
            detailLines = detailVm.lines(id);
            detailForId = id;
            detailDirty = false;
        }
        Elements els;
        els.reserve(detailLines.size());
        for (const auto& line : detailLines) els.push_back(text(line));
        return vbox(std::move(els)) | flex;
    });

    auto layout = Container::Horizontal({leftPane, detailRenderer});
    auto ui = Renderer(layout, [&] {
        return vbox({
                   hbox({
                       vbox({
                           text(" Devices (r=refresh  e=enable/disable  q=quit) ") | bold,
                           separator(),
                           searchInput->Render(),
                           separator(),
                           deviceMenu->Render() | vscroll_indicator | yframe | flex,
                       }) | size(WIDTH, EQUAL, kLeftPaneWidth) |
                           border,
                       detailRenderer->Render() | border | flex,
                   }) | flex,
                   text(" " + (confirmToggle ? confirmToggle->prompt : statusVm.text()) + " ") |
                       inverted,
               }) |
               flex;
    });

    auto root = CatchEvent(ui, [&](const Event& event) {
        if (event == Event::Custom) {  // worker posted a UI update
            detailDirty = true;        // the model may have changed under the detail pane
            dispatcher.drain();
            return true;
        }
        if (confirmToggle) {  // modal y/n — swallow everything else
            if (event == Event::Character('y')) {
                prunePending();
                pending.push_back(
                    facade.setDeviceEnabled(confirmToggle->id, confirmToggle->enable));
                confirmToggle.reset();
            } else if (event == Event::Character('n') || event == Event::Escape) {
                confirmToggle.reset();
            }
            return true;
        }
        if (event == Event::Character('e')) {  // global like 'r' (not typable in filter)
            const auto id = listVm.selectedDeviceId();
            if (!id) return true;
            const auto device = facade.findById(*id);
            if (!device) return true;
            const bool enable = device->status == core::DeviceStatus::Disabled;
            if (!enable) {
                const auto verdict = facade.canDisable(*id);
                if (!verdict.allowed) {
                    // Surface the advisory refusal on the transient status line.
                    bus.publish(
                        core::TaskCompletedEvent{.taskId = "guard",
                                                 .ok = false,
                                                 .message = "cannot disable: " + verdict.reason});
                    return true;
                }
            }
            confirmToggle = PendingToggle{
                .id = *id,
                .enable = enable,
                .prompt = std::string(enable ? "enable " : "disable ") + device->name + "? (y/n)"};
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
        if (event == Event::Character('r')) {
            // Drop already-completed refreshes so `pending` stays bounded over
            // a long session instead of growing by one future per keypress.
            prunePending();
            pending.push_back(facade.refresh());  // fire; results arrive via the dispatcher
            return true;
        }
        return false;  // let Input / Menu handle the rest (incl. mouse)
    });

    // Initial populate synchronously so the first frame is not empty and so the
    // status line stays silent for the initial enumeration (statusVm is armed
    // only afterward). Events published by refresh() are drained onto this (UI)
    // thread before the loop starts.
    //
    // The try spans from here (not just screen.Loop()) because hotplug/delayed
    // own live threads: the DelayedScheduler timer thread exists as soon as
    // `delayed` is constructed, and hotplug.start() launches the udev reader
    // thread mid-block. An exception escaping any of these pre-loop calls
    // (drain(), start(), a publish()) must therefore ALSO stop those threads
    // before the ViewModels/dispatcher/screen unwind — the same use-after-
    // destruction Deviation 2 exists to prevent. Widening the try makes that
    // safety structural rather than resting on the (currently true, but fragile
    // and cross-file) invariant that nothing schedules onto delayed or publishes
    // into a VM before the loop begins.
    try {
        facade.refresh().wait();
        dispatcher.drain();
        statusVm.arm();
        if (auto started = hotplug.start(); !started) {
            // Degrade gracefully: without live events, 'r' refresh still works.
            bus.publish(core::ErrorEvent{.source = "hotplug", .message = started.error().message});
        }
        screen.Loop(root);
    } catch (...) {
        // Exception-safe teardown (deviation from the brief, user-approved — see
        // .superpowers/sdd/task-8-notes.md Deviation 2). Without this, plain
        // reverse-declaration-order unwind would destroy statusVm/detailVm/
        // listVm/dispatcher/screen while the udev reader thread (monitor, owned by
        // hotplug) and the DelayedScheduler timer thread (delayed) are still live
        // — both call back into bus.publish() -> VM handlers -> dispatcher.post()
        // -> screen, a genuine use-after-destruction. hotplug/delayed are declared
        // before screen/dispatcher/the VMs, so they are guaranteed still alive here
        // on every exit path. try/catch (rather than a function-scope RAII guard)
        // keeps these calls and the normal-path calls below mutually exclusive, so
        // stop()/shutdown() run exactly once per exit path, and keeps them firing
        // before the function unwinds its locals — preserving the brief's documented
        // normal-path sequence (stop event sources -> drain pending refreshes ->
        // return) instead of reordering it the way a plain scope guard destroyed at
        // function-return time would. stop()/shutdown() are each idempotent regardless.
        hotplug.stop();
        delayed.shutdown();
        // Mirror the normal-path drain below: an enumeration still running on the
        // scheduler when the exception was thrown could otherwise publish into
        // listVm/detailVm/statusVm while they are being destroyed during stack
        // unwind. Wait for every submitted refresh to finish before rethrowing.
        drainPending(pending);
        throw;
    }

    // Stop event sources before the VMs/dispatcher unwind: join the monitor
    // reader thread (no new events into the debounce map), then join the timer
    // thread (no flush/clear callback can still publish into a VM being torn
    // down). Order: monitor -> timer -> drain in-flight refreshes.
    hotplug.stop();
    delayed.shutdown();

    // Teardown safety: the VMs' EventBus handlers capture `this`. An enumeration still
    // running on the scheduler would publish into a half-destroyed VM as these locals
    // unwind (the scheduler is destroyed AFTER the VMs in stack order, so its drain/join
    // happens too late). Wait for every submitted refresh to finish here — by the time a
    // refresh's future is ready, its synchronous publish→onModelChanged→dispatcher.post
    // chain has completed while the VMs are still alive. This also honors
    // ApplicationFacade::refresh()'s documented lifetime contract (the future MUST be
    // awaited before the facade is destroyed).
    drainPending(pending);
    return 0;
}

}  // namespace devmgr::tui
