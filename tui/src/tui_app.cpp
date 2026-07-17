#include "tui/src/tui_app.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/string.hpp>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/snapshots_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/app/updates_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#include "devmgr/platform/linux/kmod_driver_manager.hpp"
#include "devmgr/platform/linux/linux_system_info.hpp"
#include "devmgr/platform/linux/dkms_status_provider.hpp"
#ifdef DEVMGR_HAS_SDBUS
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"
#include "devmgr/platform/linux/fwupd_update_provider.hpp"
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

// Loop-scrolls a string that exceeds `width` cells: pause at the start,
// advance one glyph per tick to the end, pause, restart from the beginning
// (DESIGN.md §2.4 — long identifiers "scroll within a bounded region").
// Glyph-based so multi-byte names (e.g. "…Webcam™") never split mid-codepoint.
std::string marqueeWindow(const std::string& s, int width, int tick) {
    const auto glyphs = ftxui::Utf8ToGlyphs(s);
    const int n = static_cast<int>(glyphs.size());
    if (n <= width) return s;
    constexpr int kEndPauseTicks = 7;  // ~1 s at the 150 ms tick rate
    const int overflow = n - width;
    const int cycle = overflow + 2 * kEndPauseTicks;
    const int offset = std::clamp(tick % cycle - kEndPauseTicks, 0, overflow);
    std::string out;
    for (int i = offset; i < offset + width; ++i) out += glyphs[static_cast<std::size_t>(i)];
    return out;
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
    platform_linux::KmodDriverManager kmod;         // system defaults: /sys, real modules
    platform_linux::LinuxSystemInfo sysinfo;
#ifdef DEVMGR_HAS_SDBUS
    platform_linux::DbusPrivilegedChannel channel;  // system bus → devmgrd
    platform_linux::FwupdUpdateProvider fwupdProvider(bus);
#endif
    platform_linux::DkmsStatusProvider dkmsProvider;
    // Declaration order = teardown contract: providers outlive the facade (T9
    // param), which outlives the VMs below.
    std::vector<pal::IUpdateProvider*> updateProviders;
#ifdef DEVMGR_HAS_SDBUS
    updateProviders.push_back(&fwupdProvider);
#endif
    updateProviders.push_back(&dkmsProvider);
#ifdef DEVMGR_HAS_SDBUS
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, &channel, &prober, &kmod,
                                  &sysinfo, updateProviders);
#else
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, nullptr, &prober, &kmod,
                                  &sysinfo, updateProviders);
#endif
    app::HotplugService hotplug(monitor, service, delayed);  // 250 ms default window

    auto screen = ScreenInteractive::Fullscreen();
    FtxuiUiDispatcher dispatcher(screen);
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);
    app::StatusLineVM statusVm(bus, delayed, dispatcher);
    app::ModulesVM modulesVm(facade, bus, scheduler, dispatcher);
    app::UpdatesVM updatesVm(facade, bus, dispatcher);
    app::SnapshotsVM snapshotsVm(facade, bus, dispatcher);

    // Keep every refresh future alive so we can wait on them before teardown
    // (see the note after screen.Loop()).
    std::vector<std::future<void>> pending;

    struct PendingConfirm {  // y/n: device toggle, unbind, module unload
        std::function<void()> onYes;
        std::string prompt;
    };
    std::optional<PendingConfirm> confirm;
    struct PendingText {  // typed input: load module / bind driver
        std::function<void(const std::string&)> onSubmit;
        std::string prompt;
        std::string buffer;
    };
    std::optional<PendingText> textPrompt;
    auto statusLine = [&]() -> std::string {
        if (textPrompt) return textPrompt->prompt + textPrompt->buffer + "_";
        if (confirm) return confirm->prompt;
        return statusVm.text();
    };

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

    // Marquee for the selected device row (user request): when its text
    // overflows the fixed-width left pane, loop-scroll it instead of
    // truncating. Deliberate, documented deviation from DESIGN.md §4.5
    // (motion beyond a task indicator): sanctioned by §2.4's bounded-region
    // scroll, and the ticker below only fires while an overflowing row is
    // actually selected — an idle screen stays static.
    int marqueeTick = 0;                     // UI thread only (render + tick event)
    std::atomic<bool> marqueeNeeded{false};  // render thread → ticker thread
    const Event kMarqueeTick = Event::Special("devmgr-marquee-tick");
    MenuOption deviceMenuOpt = MenuOption::Vertical();
    deviceMenuOpt.entries_option.transform = [&](const EntryState& s) {
        constexpr int kRowWidth = kLeftPaneWidth - 4;  // border + "> " prefix + scrollbar
        std::string label = s.label;
        if (s.active && string_width(label) > kRowWidth) {
            marqueeNeeded = true;
            label = marqueeWindow(label, kRowWidth, marqueeTick);
        }
        Element e = text((s.active ? "> " : "  ") + label);
        if (s.focused) e = e | inverted;
        if (s.active) e = e | bold;
        return e;
    };
    auto deviceMenu = Menu(&listVm.rowsRef(), &listVm.selectedRef(), deviceMenuOpt);

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

    auto modulesMenu = Menu(&modulesVm.rowsRef(), &modulesVm.selectedRef(), MenuOption::Vertical());
    std::string bannerText;  // computed on tab entry — banner() reads sysfs, never per frame
    std::string moduleFilter;
    InputOption modFilterOpt;
    modFilterOpt.content = &moduleFilter;
    modFilterOpt.placeholder = "filter modules…";
    modFilterOpt.on_change = [&] { modulesVm.setFilter(moduleFilter); };
    auto moduleFilterInput = Input(modFilterOpt);

    // CONTROLLER AMENDMENT A-1 (user-approved 2026-07-09): detailLines() does
    // libkmod disk I/O per call (facade moduleDetail + modprobeDetail,
    // app/src/modules_vm.cpp:225-231), and FTXUI re-renders on every event —
    // cache exactly like the devices detail pane above (tui_app.cpp:113-133).
    std::optional<std::string> modDetailForName;
    std::vector<std::string> modDetailLines;
    bool modDetailDirty = true;

    auto moduleDetail = Renderer([&] {
        const auto name = modulesVm.selectedModule();
        if (modDetailDirty || name != modDetailForName) {
            modDetailLines = modulesVm.detailLines();
            modDetailForName = name;
            modDetailDirty = false;
        }
        Elements els;
        els.reserve(modDetailLines.size());
        for (const auto& line : modDetailLines) els.push_back(text(line));
        return vbox(std::move(els)) | flex;
    });
    auto modulesPane = Container::Vertical({moduleFilterInput, modulesMenu});
    auto modulesLayout = Container::Horizontal({modulesPane, moduleDetail});

    // No filter on the updates list (UpdatesVM exposes none) — mirrors modules
    // shape minus the filter input.
    auto updatesMenu = Menu(&updatesVm.rowsRef(), &updatesVm.selectedRef(), MenuOption::Vertical());

    // detailLines() is cheap (no disk/D-Bus I/O — it reads the last snapshot_,
    // T10), but still cache like the devices/modules panes above: cheap or
    // not, no work belongs in Render() that a stale cache could avoid, and it
    // keeps this pane consistent with the other two. Identity is the row
    // index (UpdatesVM exposes no stable selected-candidate accessor); a
    // rebuild that moves the same candidate to a new index just recomputes
    // once, which is harmless.
    int updDetailForIndex = -1;
    std::vector<std::string> updDetailLines;
    bool updDetailDirty = true;

    auto updatesDetail = Renderer([&] {
        const int idx = updatesVm.selectedRef();
        if (updDetailDirty || idx != updDetailForIndex) {
            updDetailLines = updatesVm.detailLines();
            updDetailForIndex = idx;
            updDetailDirty = false;
        }
        Elements els;
        els.reserve(updDetailLines.size());
        for (const auto& line : updDetailLines) els.push_back(text(line));
        return vbox(std::move(els)) | flex;
    });
    auto updatesLayout = Container::Horizontal({updatesMenu, updatesDetail});

    // Snapshots tab (backup-rollback-engine, snapshot-ui spec): no filter —
    // mirrors the updates shape.
    auto snapshotsMenu =
        Menu(&snapshotsVm.rowsRef(), &snapshotsVm.selectedRef(), MenuOption::Vertical());

    // detailLines() is cheap (reads the last rebuilt metas_), but cache like
    // the other panes (A-1 idiom): identity is the row index.
    int snapDetailForIndex = -1;
    std::vector<std::string> snapDetailLines;
    bool snapDetailDirty = true;

    auto snapshotsDetail = Renderer([&] {
        const int idx = snapshotsVm.selectedRef();
        if (snapDetailDirty || idx != snapDetailForIndex) {
            snapDetailLines = snapshotsVm.detailLines();
            snapDetailForIndex = idx;
            snapDetailDirty = false;
        }
        Elements els;
        els.reserve(snapDetailLines.size());
        for (const auto& line : snapDetailLines) els.push_back(text(line));
        return vbox(std::move(els)) | flex;
    });
    auto snapshotsLayout = Container::Horizontal({snapshotsMenu, snapshotsDetail});

    // Status/prompt line for the updates tab (DESIGN.md §3.2: one row, stable
    // edge): modal text wins, then the durable install-progress text (spec
    // §5.5), else the shared transient status line.
    auto updatesStatusLine = [&]() -> std::string {
        if (textPrompt) return textPrompt->prompt + textPrompt->buffer + "_";
        if (confirm) return confirm->prompt;
        const auto progress = updatesVm.installProgressText();
        return progress.empty() ? statusVm.text() : progress;
    };

    int activeTab = 0;  // 0 = devices, 1 = modules, 2 = updates, 3 = snapshots
    auto tabs = Container::Tab({layout, modulesLayout, updatesLayout, snapshotsLayout}, &activeTab);

    // Tab titles line: names all three views with their direct-access digit
    // (parity with the GUI's persistent tab bar, DESIGN.md §9 Primary
    // navigation); only the active one is bold — the rest of each header
    // keeps the existing per-tab bold-legend convention below it. Letters
    // were considered for the hints but collide with existing verbs
    // (d=dismiss, u=install/unload, U=unbind), so digits it is; 'm' still
    // cycles.
    auto tabTitles = [&] {
        auto name = [&](const char* key, const char* label, int tab) {
            Element e = hbox({text(std::string("[") + key + "]"), text(label)});
            return activeTab == tab ? e | bold : e;
        };
        return hbox({text(" "), name("1", "Devices", 0), text(" | "), name("2", "Modules", 1),
                     text(" | "), name("3", "Updates", 2), text(" | "), name("4", "Snapshots", 3),
                     text("  (m: next tab) ")});
    };

    auto ui = Renderer(tabs, [&] {
        marqueeNeeded = false;  // re-set by the menu transform while an overflowing row is selected
        if (activeTab == 1) {
            return vbox({
                       tabTitles(),
                       text(" Modules (/=filter  l=load  u=unload  q=quit) ") | bold,
                       text(" " + bannerText + " "),
                       separator(),
                       hbox({
                           vbox({
                               moduleFilterInput->Render(),
                               separator(),
                               modulesMenu->Render() | vscroll_indicator | yframe | flex,
                           }) | size(WIDTH, EQUAL, 72) |
                               border,
                           moduleDetail->Render() | border | flex,
                       }) | flex,
                       text(" " + statusLine() + " ") | inverted,
                   }) |
                   flex;
        }
        if (activeTab == 2) {
            Elements top = {
                tabTitles(),
                text(" Updates (u=install  r=refresh  d=dismiss  q=quit) ") | bold,
                text(" " + bannerText + " "),
            };
            const auto reqBanner = updatesVm.requestBanner();
            if (!reqBanner.empty()) top.push_back(text(" " + reqBanner + " ") | bold);
            top.push_back(separator());
            top.push_back(hbox({
                              vbox({
                                  updatesMenu->Render() | vscroll_indicator | yframe | flex,
                              }) | size(WIDTH, EQUAL, 72) |
                                  border,
                              updatesDetail->Render() | border | flex,
                          }) |
                          flex);
            top.push_back(text(" " + updatesStatusLine() + " ") | inverted);
            return vbox(std::move(top)) | flex;
        }
        if (activeTab == 3) {
            return vbox({
                       tabTitles(),
                       text(" Snapshots (s=create…  r=restore  x=delete  q=quit) ") | bold,
                       text(" " + bannerText + " "),
                       separator(),
                       hbox({
                           vbox({
                               snapshotsMenu->Render() | vscroll_indicator | yframe | flex,
                           }) | size(WIDTH, EQUAL, 72) |
                               border,
                           snapshotsDetail->Render() | border | flex,
                       }) | flex,
                       text(" " + statusLine() + " ") | inverted,
                   }) |
                   flex;
        }
        // Legend is a full-width row like the other two tabs (it used to live
        // inside the 44-column left pane, where it truncated mid-shortcut).
        return vbox({
                   tabTitles(),
                   text(" Devices (/=filter  r=refresh  e=enable/disable  U=unbind  B=bind  "
                        "q=quit) ") |
                       bold,
                   separator(),
                   hbox({
                       vbox({
                           searchInput->Render(),
                           separator(),
                           deviceMenu->Render() | vscroll_indicator | yframe | flex,
                       }) | size(WIDTH, EQUAL, kLeftPaneWidth) |
                           border,
                       detailRenderer->Render() | border | flex,
                   }) | flex,
                   text(" " + statusLine() + " ") | inverted,
               }) |
               flex;
    });

    // Single tab-entry path shared by the 'm' cycle and the direct 1/2/3
    // keys, so the per-tab side effects (banner recompute, rebuild, updates
    // refresh) can never diverge between the two routes. Focus lands on the
    // tab's list: predictable, and it keeps single-key verbs live (the filter
    // guard below routes keys to a filter Input only while it owns focus).
    auto switchToTab = [&](int tab) {
        activeTab = tab;
        if (tab == 1) {
            bannerText = modulesVm.banner();
            modulesVm.rebuild();
            modDetailDirty = true;       // A-1: fresh snapshot under the cache
            modulesVm.fillSignatures();  // cached names are skipped
            modulesMenu->TakeFocus();
        } else if (tab == 2) {
            bannerText = updatesVm.banner();
            updatesVm.rebuild();
            updDetailDirty = true;  // A-1 idiom: fresh snapshot under the cache
            prunePending();
            pending.push_back(facade.refreshUpdates());  // fresh data on entry
            updatesMenu->TakeFocus();
        } else if (tab == 3) {
            snapshotsVm.rebuild();
            bannerText = snapshotsVm.banner();  // after rebuild: banner reads the rebuilt metas
            snapDetailDirty = true;             // A-1 idiom: fresh snapshot under the cache
            prunePending();
            pending.push_back(facade.refreshSnapshots());  // fresh data on entry
            snapshotsMenu->TakeFocus();
        } else {
            deviceMenu->TakeFocus();
        }
    };

    auto root = CatchEvent(ui, [&](const Event& event) {
        if (event == kMarqueeTick) {
            ++marqueeTick;
            return true;  // handled → FTXUI re-renders → the transform scrolls
        }
        if (event == Event::Custom) {
            detailDirty = true;
            modDetailDirty = true;   // A-1: drained posts may rebuild the modules model
            updDetailDirty = true;   // A-1 idiom: drained posts may rebuild the updates model
            snapDetailDirty = true;  // A-1 idiom: drained posts may rebuild the snapshots model
            dispatcher.drain();
            // Review finding I-1: banner() queries the PAL (systemInfo()) and must
            // never run in Render() (DESIGN.md §8) — recompute it here, the same
            // drain point that just applied a queued rebuild()/refresh completion,
            // so availability/version/"reboot required" don't go stale between tab
            // entries. Gated to the active tab only — same reasoning as the other
            // two A-1 dirty flags above.
            if (activeTab == 2) bannerText = updatesVm.banner();
            if (activeTab == 3) bannerText = snapshotsVm.banner();
            return true;
        }
        if (textPrompt) {  // modal typed input
            if (event == Event::Return) {
                auto submit = std::move(textPrompt->onSubmit);
                const std::string value = textPrompt->buffer;
                textPrompt.reset();
                if (!value.empty()) submit(value);
            } else if (event == Event::Escape) {
                textPrompt.reset();
            } else if (event == Event::Backspace && !textPrompt->buffer.empty()) {
                textPrompt->buffer.pop_back();
            } else if (event.is_character()) {
                const char c = event.character()[0];
                if ((std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_' || c == '-')
                    textPrompt->buffer += c;
            }
            return true;
        }
        if (confirm) {  // modal y/n — swallow everything else
            if (event == Event::Character('y')) {
                auto go = std::move(confirm->onYes);
                confirm.reset();
                go();
            } else if (event == Event::Character('n') || event == Event::Escape) {
                confirm.reset();
            }
            return true;
        }
        // While a filter Input owns focus, printable keys belong to the
        // filter, never to single-key commands (typing 'U' must not unbind a
        // driver mid-search). Enter hands focus back to the list keeping the
        // filter text; Escape also clears it (DESIGN.md §5.1 "a direct way to
        // clear the filter").
        const Component filterInput = activeTab == 0   ? searchInput
                                      : activeTab == 1 ? moduleFilterInput
                                                       : nullptr;
        if (filterInput && filterInput->Focused()) {
            const Component menu = activeTab == 0 ? deviceMenu : modulesMenu;
            if (event == Event::Return) {
                menu->TakeFocus();
                return true;
            }
            if (event == Event::Escape) {
                if (activeTab == 0) {
                    filter.clear();
                    listVm.setFilter(filter);
                } else {
                    moduleFilter.clear();
                    modulesVm.setFilter(moduleFilter);
                }
                menu->TakeFocus();
                return true;
            }
            return false;  // characters, backspace, arrows, mouse → the Input
        }
        if (event == Event::Character('/') &&
            (activeTab == 0 || activeTab == 1)) {  // updates/snapshots have no filter
            (activeTab == 0 ? searchInput : moduleFilterInput)->TakeFocus();
            return true;
        }
        if (event == Event::Character('m')) {
            switchToTab((activeTab + 1) % 4);  // Devices → Modules → Updates → Snapshots → …
            return true;
        }
        if (event == Event::Character('1')) {
            switchToTab(0);
            return true;
        }
        if (event == Event::Character('2')) {
            switchToTab(1);
            return true;
        }
        if (event == Event::Character('3')) {
            switchToTab(2);
            return true;
        }
        if (event == Event::Character('4')) {
            switchToTab(3);
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            if (facade.installActive()) {
                confirm =
                    PendingConfirm{.onYes = [&] { screen.Exit(); },
                                   .prompt =
                                       "firmware flash continues in the fwupd daemon; closing does "
                                       "NOT cancel it. quit? (y/n)"};
                return true;
            }
            screen.Exit();
            return true;
        }
        if (activeTab == 1) {  // ----- modules keys -----
            if (event == Event::Character('l')) {
                textPrompt = PendingText{.onSubmit =
                                             [&](const std::string& name) {
                                                 prunePending();
                                                 pending.push_back(facade.loadModule(name));
                                             },
                                         .prompt = "load module: ",
                                         .buffer = ""};
                return true;
            }
            if (event == Event::Character('u')) {
                const auto name = modulesVm.selectedModule();
                if (!name) return true;
                const auto verdict = facade.canUnloadModule(*name);
                if (!verdict.allowed) {
                    bus.publish(
                        core::TaskCompletedEvent{.taskId = "guard",
                                                 .ok = false,
                                                 .message = "cannot unload: " + verdict.reason});
                    return true;
                }
                confirm = PendingConfirm{.onYes =
                                             [&, name = *name] {
                                                 prunePending();
                                                 pending.push_back(facade.unloadModule(name));
                                             },
                                         .prompt = "unload module " + *name + "? (y/n)"};
                return true;
            }
            return false;  // filter input / menu / mouse
        }
        if (activeTab == 2) {  // ----- updates keys -----
            if (event == Event::Character('u')) {
                const auto args = updatesVm.selectedInstall();
                if (!args) {
                    bus.publish(core::TaskCompletedEvent{
                        .taskId = "guard",
                        .ok = false,
                        .message = "not installable from here (status-only or external "
                                   "download — run `fwupdmgr update`)"});
                    return true;
                }
                confirm = PendingConfirm{.onYes =
                                             [&, a = *args] {
                                                 prunePending();
                                                 pending.push_back(facade.installUpdate(
                                                     a.providerId, a.candidateId, a.release));
                                             },
                                         .prompt = args->confirmText + " (y/n)"};
                return true;
            }
            if (event == Event::Character('r')) {
                prunePending();
                pending.push_back(facade.refreshUpdates());
                return true;
            }
            if (event == Event::Character('d')) {
                updatesVm.dismissRequest();
                return true;
            }
            return false;  // filter input / menu / mouse
        }
        if (activeTab == 3) {                      // ----- snapshots keys -----
            if (event == Event::Character('s')) {  // create manual snapshot (label prompt)
                textPrompt = PendingText{.onSubmit =
                                             [&](const std::string& label) {
                                                 prunePending();
                                                 pending.push_back(facade.createSnapshot(label));
                                             },
                                         .prompt = "snapshot label: ",
                                         .buffer = ""};
                return true;
            }
            if (event == Event::Character('r')) {  // restore selected (confirm modal)
                const auto args = snapshotsVm.selectedRestore();
                if (!args) {
                    // Refused locally: placeholder row or corrupt/unsupported
                    // snapshot (DESIGN.md §5.3: the key stays documented, the
                    // status line explains the refusal).
                    bus.publish(core::TaskCompletedEvent{
                        .taskId = "guard",
                        .ok = false,
                        .message = "cannot restore: no healthy snapshot selected"});
                    return true;
                }
                confirm = PendingConfirm{.onYes =
                                             [&, a = *args] {
                                                 prunePending();
                                                 pending.push_back(facade.restoreSnapshot(a.id));
                                             },
                                         .prompt = args->confirmText + " (y/n)"};
                return true;
            }
            if (event == Event::Character('x')) {  // delete selected (confirm modal)
                const auto args = snapshotsVm.selectedDelete();
                if (!args) {
                    bus.publish(core::TaskCompletedEvent{
                        .taskId = "guard",
                        .ok = false,
                        .message = "cannot delete: no deletable snapshot selected"});
                    return true;
                }
                confirm = PendingConfirm{.onYes =
                                             [&, a = *args] {
                                                 prunePending();
                                                 pending.push_back(facade.deleteSnapshot(a.id));
                                             },
                                         .prompt = args->confirmText + " (y/n)"};
                return true;
            }
            return false;  // menu / mouse
        }
        // ----- devices keys (activeTab == 0) -----
        if (event == Event::Character('e')) {  // list focused — the filter guard above ran
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
            confirm = PendingConfirm{
                .onYes =
                    [&, id = *id, enable] {
                        prunePending();
                        pending.push_back(facade.setDeviceEnabled(id, enable));
                    },
                .prompt = std::string(enable ? "enable " : "disable ") + device->name + "? (y/n)"};
            return true;
        }
        if (event == Event::Character('U')) {  // surgical unbind (advanced)
            const auto id = listVm.selectedDeviceId();
            const auto device = id ? facade.findById(*id) : std::nullopt;
            if (!device) return true;
            const auto verdict = facade.canDisable(*id);
            if (!verdict.allowed) {
                bus.publish(core::TaskCompletedEvent{
                    .taskId = "guard", .ok = false, .message = "cannot unbind: " + verdict.reason});
                return true;
            }
            confirm = PendingConfirm{.onYes =
                                         [&, id = *id] {
                                             prunePending();
                                             pending.push_back(facade.unbindDriver(id));
                                         },
                                     .prompt = "unbind driver from " + device->name +
                                               "? (advanced, not persistent) (y/n)"};
            return true;
        }
        if (event == Event::Character('B')) {  // surgical bind (advanced)
            const auto id = listVm.selectedDeviceId();
            const auto device = id ? facade.findById(*id) : std::nullopt;
            if (!device) return true;
            std::string prefill = device->boundDriver.value_or("");
            if (prefill.empty()) {
                const auto candidates = facade.driverInfo(*id);
                if (!candidates.empty()) prefill = candidates.front().name;
            }
            textPrompt = PendingText{.onSubmit =
                                         [&, id = *id](const std::string& driver) {
                                             prunePending();
                                             pending.push_back(facade.bindDriver(id, driver));
                                         },
                                     .prompt = "bind driver to " + device->name + ": ",
                                     .buffer = prefill};
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

    // The list, not the filter, owns focus at startup so single-key verbs work
    // immediately; '/' reaches the filter.
    deviceMenu->TakeFocus();

    // Marquee ticker: wakes every 150 ms but posts (→ re-render) only while
    // the last render flagged an overflowing selected row — an idle screen
    // stays static (DESIGN.md §4.5). Joined on both exit paths below, before
    // the screen and VM locals unwind.
    std::atomic<bool> marqueeTickerRun{true};
    std::thread marqueeTicker([&] {
        while (marqueeTickerRun.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            if (marqueeTickerRun.load() && marqueeNeeded.load()) screen.PostEvent(kMarqueeTick);
        }
    });
    auto stopMarqueeTicker = [&] {
        marqueeTickerRun = false;
        if (marqueeTicker.joinable()) marqueeTicker.join();
    };

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
        stopMarqueeTicker();  // ticker posts into `screen` — join before unwind
        hotplug.stop();
        delayed.shutdown();
        // Mirror the normal-path drain below: an enumeration still running on the
        // scheduler when the exception was thrown could otherwise publish into
        // listVm/detailVm/statusVm while they are being destroyed during stack
        // unwind. Wait for every submitted refresh to finish before rethrowing.
        drainPending(pending);
        throw;
    }

    // Stop event sources before the VMs/dispatcher unwind: join the marquee
    // ticker (posts into `screen`), then the monitor reader thread (no new
    // events into the debounce map), then the timer thread (no flush/clear
    // callback can still publish into a VM being torn down). Order:
    // ticker -> monitor -> timer -> drain in-flight refreshes.
    stopMarqueeTicker();
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
