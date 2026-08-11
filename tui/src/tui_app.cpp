#include "tui/src/tui_app.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

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
#include "devmgr/pal/platform_backends.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "tui/src/ftxui_ui_dispatcher.hpp"
#include "tui/src/key_routing.hpp"
#include "tui/src/render_util.hpp"
#include "tui/src/selection.hpp"
#include "tui/src/state_roles.hpp"
#include "tui/src/views/detail_pane.hpp"
#include "tui/src/views/devices_view.hpp"
#include "tui/src/views/min_size.hpp"
#include "tui/src/views/modules_view.hpp"
#include "tui/src/views/pane_layout.hpp"
#include "tui/src/views/snapshots_view.hpp"
#include "tui/src/views/updates_view.hpp"

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

// runTuiApp is the top-level FTXUI event-loop composition — component tree,
// keybindings, and state wiring for the whole app. Like gui/main_window.cpp's
// constructor (its GUI sibling, suppressed the same way) it is irreducibly large
// by nature; the render logic it drives is already extracted into pure functions
// under tui/src/views/*. Suppress size/complexity here rather than fragment the
// event loop into artificial helpers that only obscure control flow.
// NOLINTBEGIN(readability-function-cognitive-complexity,readability-function-size)
int runTuiApp(bool selfTest, const Theme& theme) {
    using namespace ftxui;

    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    runtime::DelayedScheduler delayed;

    // The one place a platform enters this program. `backends` OWNS every
    // implementation; declaring it here keeps the teardown contract identical
    // to runGuiApp() — destroyed after the VMs, the facade and HotplugService,
    // and before the schedulers and the bus the backends were handed.
    pal::BackendOptions backendOptions;
    backendOptions.eventBus = &bus;
    auto backends = pal::PlatformBackends::create(backendOptions);
    if (!backends) {
        // No screen, no threads, nothing wired — fail before the alternate
        // screen buffer is ever entered, so the message stays readable.
        std::cerr << "devmgr-tui: cannot start: " << backends.error().message << "\n";
        return 1;
    }
    const pal::BackendSet& backendSet = (*backends)->backends();

    app::DeviceService service(bus);
    app::ApplicationFacade facade(backendSet, (*backends)->capabilities(), scheduler, bus, service);
    app::HotplugService hotplug(backendSet.hotplug, service, delayed);  // 250 ms default window

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
    // Restore preview (snapshot-ui spec): replaces the plain y/n confirm for
    // restore. Modal like the others — it renders over the tab body and
    // swallows unrelated input — but multi-line, because it carries the diff,
    // the selected/current/last-good markers and the convergence note.
    struct PendingPreview {
        std::function<void()> onConfirm;
    };
    std::optional<PendingPreview> preview;
    // The status row's text and its valence are decided together (§4.1): an
    // active modal is an interactive prompt and shows neutral; otherwise the
    // ViewModel-owned message carries its own severity. One function, so the two
    // can never drift into disagreeing about which source the row is showing.
    auto statusRow = [&]() -> StatusRow {
        if (textPrompt) return {.text = textPrompt->prompt + textPrompt->buffer + "_"};
        if (confirm) return {.text = confirm->prompt};
        if (preview) return {.text = "restore this snapshot? (y/n)"};
        return composeStatus({{statusVm.text(), statusVm.severity()}});
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
    // Modules/Updates/Snapshots use a wider left pane than Devices (their rows
    // carry longer identifiers); shared here so the three views stay in step.
    // Computed from the LIVE terminal width, not fixed: a hard 72 left the
    // detail pane six columns wide at the 80-column minimum, where it could
    // render nothing (views::wideLeftPaneWidth).
    auto widePaneWidth = [&] { return views::wideLeftPaneWidth(screen.dimx()); };

    std::string filter;
    InputOption inputOpt;
    inputOpt.content = &filter;
    inputOpt.placeholder = "filter devices…";
    inputOpt.on_change = [&] { listVm.setFilter(filter); };
    auto searchInput = Input(inputOpt);

    // Bounded reveal of an overflowing selected row (design Decision 9, R3).
    // Pass 1 loop-scrolled the Devices row forever (`tick % cycle`), which is
    // exactly the idle decoration DESIGN.md §4.5 forbids and needs a redraw loop
    // that never stops. The reveal now slides once and RESTS: the pure offset
    // maths lives in render_util, and this tick counter is the single live time
    // source — reset whenever the selection moves, and advanced only while
    // `revealRunning` says an overflowing row is selected and not yet at rest.
    // All four lists share it, not just Devices.
    int revealTick = 0;                      // UI thread only (render + tick event)
    std::atomic<bool> revealRunning{false};  // render thread → ticker thread
    // (tab, selected row) the current reveal belongs to; a change restarts it.
    std::pair<int, int> revealKey{-1, -1};
    const Event kRevealTick = Event::Special("devmgr-reveal-tick");
    // Two narrower than the pane: the border, the "> " prefix and the scroll
    // affordance, plus a glyph+space for the lists that carry a status glyph.
    static constexpr int kRowWidthMargin = 6;
    // Tracks the pane, so the bounded reveal windows a row to the width the row
    // is actually given — a fixed 66 here would window past the pane's right
    // edge at 80 columns and the tail would never come into view.
    auto wideRowWidth = [&] { return widePaneWidth() - kRowWidthMargin; };
    // A criticality badge costs the row its glyph plus a space (R4).
    static constexpr int kBadgeWidth = 2;
    // Windows `label` for the selected row of a list whose rows are `rowWidth`
    // cells wide, and reports (via revealRunning) whether the reveal still has
    // somewhere to go. Non-selected rows are not windowed at all: render::menuRow
    // elides them on the right.
    auto revealLabel = [&](const std::string& label, int rowWidth) {
        const int maxOffset = render::revealMaxOffset(label, rowWidth);
        if (maxOffset == 0) return label;  // fits: static, no ticker
        const int offset = render::revealOffset(revealTick, maxOffset);
        if (offset < maxOffset) revealRunning = true;
        return render::revealWindow(label, rowWidth, offset);
    };
    // Declared before the option so the entry transform can ask the menu whether
    // it owns the keyboard (B1); assigned immediately below.
    Component deviceMenu;
    MenuOption deviceMenuOpt = MenuOption::Vertical();
    deviceMenuOpt.entries_option.transform = [&](const EntryState& s) {
        const bool hasBadge = badgeForCriticality(listVm.criticalityForRow(s.index)).has_value();
        const int kRowWidth = kLeftPaneWidth - kRowWidthMargin - (hasBadge ? kBadgeWidth : 0);
        const std::string label = s.active ? revealLabel(s.label, kRowWidth) : s.label;
        // B1: every selection signal keys off the selected index and the list's
        // own focus, never off FTXUI's focused entry (the mouse moves that one
        // independently of `selected`, which is what split the marker, the bar
        // and the detail pane across three rows in pass 1).
        const bool listFocused = deviceMenu->Focused();
        // Group headers and the "(no devices)" placeholder carry no status: mute
        // them (§4.3), add no glyph, and never show the cursor on them (B3 — the
        // snap below keeps the selection off them, and this makes the empty list,
        // where nothing at all is selectable, render with no cursor).
        if (listVm.isHeader(s.index))
            return views::renderDeviceRow(label, /*selected=*/false, listFocused, std::nullopt,
                                          Role::Muted, theme);
        const auto status = listVm.statusForRow(s.index);
        return views::renderDeviceRow(label, s.active, listFocused, glyphForDeviceStatus(status),
                                      roleForDeviceStatus(status), theme,
                                      badgeForCriticality(listVm.criticalityForRow(s.index)));
    };
    deviceMenu = Menu(&listVm.rowsRef(), &listVm.selectedRef(), deviceMenuOpt);

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
        return views::renderDetailPane(detailLines, theme);
    });

    auto layout = Container::Horizontal({leftPane, detailRenderer});

    Component modulesMenu;  // declared first for the B1 focus query, assigned below
    MenuOption modulesMenuOpt = MenuOption::Vertical();
    modulesMenuOpt.entries_option.transform = [&](const EntryState& s) {
        // Colour by signature state; the "yes/NO/…" cell already in the row is the
        // paired non-colour signal (§10), so no glyph is added. A row with no
        // signature state is the placeholder — never the cursor (B3).
        const auto sig = modulesVm.signedForRow(s.index);
        const bool selected = s.active && sig.has_value();
        // R4: the criticality badge is its own element with its own warning
        // role, so it never recolours the signature cell inside the label.
        const auto badge = badgeForCriticality(modulesVm.criticalityForRow(s.index));
        const int rowWidth = badge ? wideRowWidth() - kBadgeWidth : wideRowWidth();
        const std::string label = selected ? revealLabel(s.label, rowWidth) : s.label;
        return render::menuRow(label, selected, modulesMenu->Focused(), std::nullopt,
                               roleForSignature(sig), theme, badge);
    };
    modulesMenu = Menu(&modulesVm.rowsRef(), &modulesVm.selectedRef(), modulesMenuOpt);
    std::string bannerText;  // computed on tab entry — banner() reads sysfs, never per frame
    // Backend availability for the Updates banner, recomputed with bannerText at
    // the same points (never in Render()). The role and glyph come FROM the VM;
    // the render path never inspects the banner string to decide how loud it is.
    std::optional<Role> modulesBannerRole;  // supplied by ModulesVM, never parsed back out
    std::optional<Role> updatesBannerRole;
    std::optional<render::Glyph> updatesBannerGlyph;
    std::vector<std::string> updatesDiagnostics;
    bool showDiagnostics = false;  // `i` toggle; inert while updatesDiagnostics is empty
    // devmgrd's note, recomputed alongside each tab's banner. Every view that
    // the daemon feeds reads THIS — one shared BackendStatusVM behind it, so a
    // single transition is logged once no matter how many tabs are watching.
    std::optional<Role> daemonBannerRole;
    std::optional<render::Glyph> daemonBannerGlyph;
    std::vector<std::string> daemonDiagnostics;
    std::string daemonSentence;  // "" while devmgrd is serving
    auto refreshDaemonNote = [&] {
        const auto notes = snapshotsVm.availabilityNotes();  // devmgrd only
        daemonDiagnostics = app::diagnosticLines(notes);
        if (notes.empty()) {
            daemonBannerRole.reset();
            daemonBannerGlyph.reset();
            daemonSentence.clear();
            return;
        }
        daemonSentence = notes.front().text;
        daemonBannerRole = roleForSeverity(notes.front().role);
        daemonBannerGlyph = render::Glyph::Unavailable;
    };
    auto refreshUpdatesBanner = [&] {
        bannerText = updatesVm.banner();
        const auto notes = updatesVm.availabilityNotes();
        updatesDiagnostics = app::diagnosticLines(notes);
        if (notes.empty()) {
            updatesBannerRole.reset();
            updatesBannerGlyph.reset();
            showDiagnostics = false;  // nothing left to reveal; don't leave a stale region open
            return;
        }
        const bool warn = std::ranges::any_of(notes, [](const app::BackendNote& n) {
            return n.role == app::StatusSeverity::Warning;
        });
        updatesBannerRole =
            roleForSeverity(warn ? app::StatusSeverity::Warning : app::StatusSeverity::Info);
        updatesBannerGlyph = render::Glyph::Unavailable;
    };
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
        return views::renderDetailPane(modDetailLines, theme);
    });
    auto modulesPane = Container::Vertical({moduleFilterInput, modulesMenu});
    auto modulesLayout = Container::Horizontal({modulesPane, moduleDetail});

    // No filter on the updates list (UpdatesVM exposes none) — mirrors modules
    // shape minus the filter input.
    Component updatesMenu;  // declared first for the B1 focus query, assigned below
    MenuOption updatesMenuOpt = MenuOption::Vertical();
    updatesMenuOpt.entries_option.transform = [&](const EntryState& s) {
        // Colour by availability; the "-> <version>"/marker text in the row is the
        // paired non-colour signal (§10). No state == the placeholder row, which
        // never takes the cursor (B3).
        const auto state = updatesVm.stateForRow(s.index);
        const bool selected = s.active && state.has_value();
        const std::string label = selected ? revealLabel(s.label, wideRowWidth()) : s.label;
        return render::menuRow(label, selected, updatesMenu->Focused(), std::nullopt,
                               roleForUpdateState(state), theme);
    };
    updatesMenu = Menu(&updatesVm.rowsRef(), &updatesVm.selectedRef(), updatesMenuOpt);

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
        return views::renderDetailPane(updDetailLines, theme);
    });
    auto updatesLayout = Container::Horizontal({updatesMenu, updatesDetail});

    // Snapshots tab (backup-rollback-engine, snapshot-ui spec). beta-06 task
    // 3.2 gives it the same filter interaction as Devices/Modules.
    std::string snapshotFilter;
    InputOption snapFilterOpt;
    snapFilterOpt.content = &snapshotFilter;
    snapFilterOpt.placeholder = "filter snapshots…";
    snapFilterOpt.on_change = [&] { snapshotsVm.setFilter(snapshotFilter); };
    auto snapshotFilterInput = Input(snapFilterOpt);

    Component snapshotsMenu;  // declared first for the B1 focus query, assigned below
    MenuOption snapshotsMenuOpt = MenuOption::Vertical();
    snapshotsMenuOpt.entries_option.transform = [&](const EntryState& s) {
        // Colour by health, with HEAD/last-good taking the accent when healthy;
        // the health word (non-Ok rows) and the history-view chain markers are the
        // paired non-colour signals (§10). No health == the placeholder row, which
        // never takes the cursor (B3).
        const auto health = snapshotsVm.healthForRow(s.index);
        const bool selected = s.active && health.has_value();
        const std::string label = selected ? revealLabel(s.label, wideRowWidth()) : s.label;
        return render::menuRow(label, selected, snapshotsMenu->Focused(), std::nullopt,
                               roleForSnapshotRow(health, snapshotsVm.isHeadRow(s.index),
                                                  snapshotsVm.isLastGoodRow(s.index)),
                               theme);
    };
    snapshotsMenu = Menu(&snapshotsVm.rowsRef(), &snapshotsVm.selectedRef(), snapshotsMenuOpt);

    // detailLines() is cheap (reads the last rebuilt metas_), but cache like
    // the other panes (A-1 idiom): identity is the row index.
    int snapDetailForIndex = -1;
    std::vector<std::string> snapDetailLines;
    bool snapDetailDirty = true;

    // 'd' swaps the detail pane for the selected snapshot's diff against live
    // state. Not cached: diffLines() reads the VM's already-fetched result and
    // its content changes underneath us when the fetch lands.
    bool snapDiffView = false;
    std::string snapDiffForId;

    auto snapshotsDetail = Renderer([&] {
        if (snapDiffView) {
            Elements els;
            els.push_back(
                text("Differences: " + core::snapshotShortId(snapDiffForId) + " -> current state") |
                bold);
            els.push_back(separator());
            for (const auto& line : snapshotsVm.diffLines())
                els.push_back(render::elidedText(line, theme));
            return vbox(std::move(els)) | flex;
        }
        const int idx = snapshotsVm.selectedRef();
        if (snapDetailDirty || idx != snapDetailForIndex) {
            snapDetailLines = snapshotsVm.detailLines();
            snapDetailForIndex = idx;
            snapDetailDirty = false;
        }
        return views::renderDetailPane(snapDetailLines, theme);
    });
    auto snapshotsLayout =
        Container::Horizontal({snapshotFilterInput, snapshotsMenu, snapshotsDetail});

    // Status/prompt row for the updates tab (DESIGN.md §3.2: one row, stable
    // edge). A modal still owns the row outright, but the install-progress text
    // (spec §5.5) and the shared status message now COMPOSE instead of one
    // shadowing the other: `UpdatesVM::onCompleted` clears the progress text only
    // for install task ids, so a guard refusal raised during an install
    // ("not installable from here…", Danger) used to be dropped from the row
    // entirely. Progress itself is neutral; composeStatus takes the max severity,
    // so the refusal keeps both its words and its colour (K2).
    auto updatesStatusRow = [&]() -> StatusRow {
        if (textPrompt) return {.text = textPrompt->prompt + textPrompt->buffer + "_"};
        if (confirm) return {.text = confirm->prompt};
        return composeStatus({{updatesVm.installProgressText(), app::StatusSeverity::Ok},
                              {statusVm.text(), statusVm.severity()}});
    };

    int activeTab = 0;  // 0 = devices, 1 = modules, 2 = updates, 3 = snapshots
    auto tabs = Container::Tab({layout, modulesLayout, updatesLayout, snapshotsLayout}, &activeTab);

    // B3: the cursor may only rest on a data row. Which rows those are comes
    // from the ViewModels — Devices name their headers directly, the other three
    // return no per-row state for their "(no …)" placeholder — so the frontend
    // re-derives nothing (§11).
    const nav::Selectable deviceSelectable = [&](int row) { return !listVm.isHeader(row); };
    const nav::Selectable moduleSelectable = [&](int row) {
        return modulesVm.signedForRow(row).has_value();
    };
    const nav::Selectable updateSelectable = [&](int row) {
        return updatesVm.stateForRow(row).has_value();
    };
    const nav::Selectable snapshotSelectable = [&](int row) {
        return snapshotsVm.healthForRow(row).has_value();
    };
    // Per-list memory of where the cursor was, so the snap can continue the
    // direction of travel instead of always falling downwards past a header.
    int lastDeviceRow = 0;
    int lastModuleRow = 0;
    int lastUpdateRow = 0;
    int lastSnapshotRow = 0;
    // Applied once per render pass for the active tab only. Render-time fix-up
    // is deliberate and mirrors FTXUI's own Menu::Clamp(): it is the single
    // point every path that can move the selection (arrow keys, a mouse click on
    // a header, a rebuild that changed the row layout) funnels through, and the
    // snap itself is pure and idempotent.
    auto snapSelection = [](int& selected, int& last, std::size_t rowCount,
                            const nav::Selectable& selectable) {
        const int dir = selected > last ? 1 : (selected < last ? -1 : 0);
        selected = nav::snapToSelectable(selected, static_cast<int>(rowCount), dir, selectable);
        last = selected;
    };

    // Each tab body is a pure per-view render function (tui/src/views/): the
    // shell hands it the active tab, the pre-rendered interactive components and
    // the resolved theme; the view composes tab bar, legend, master-detail split
    // and status line. Extracted with no behaviour change (DESIGN.md §9
    // navigation, §3.2 status edge).
    auto ui = Renderer(tabs, [&] {
        revealRunning = false;  // re-set by the menu transform while a reveal is still moving
        if (activeTab == 1) {
            snapSelection(modulesVm.selectedRef(), lastModuleRow, modulesVm.rowsRef().size(),
                          moduleSelectable);
        } else if (activeTab == 2) {
            snapSelection(updatesVm.selectedRef(), lastUpdateRow, updatesVm.rowsRef().size(),
                          updateSelectable);
        } else if (activeTab == 3) {
            snapSelection(snapshotsVm.selectedRef(), lastSnapshotRow, snapshotsVm.rowsRef().size(),
                          snapshotSelectable);
        } else {
            snapSelection(listVm.selectedRef(), lastDeviceRow, listVm.rowsRef().size(),
                          deviceSelectable);
        }
        // Restart the reveal whenever the cursor lands somewhere else — read
        // AFTER the snap, so a cursor nudged off a header does not register as a
        // second move. The new row's name must be readable from its start rather
        // than resumed mid-slide.
        const int selectedRow = activeTab == 1   ? modulesVm.selectedRef()
                                : activeTab == 2 ? updatesVm.selectedRef()
                                : activeTab == 3 ? snapshotsVm.selectedRef()
                                                 : listVm.selectedRef();
        if (revealKey != std::pair{activeTab, selectedRow}) {
            revealKey = {activeTab, selectedRow};
            revealTick = 0;
        }
        // Minimum-size guard (DESIGN.md §3.2): below 80x24 the list/detail split
        // would overflow the screen, so show a concise message instead. 'q'
        // still quits (the CatchEvent wrapper is unaffected) and a resize
        // re-renders straight back into the full UI. The toggle boundary and the
        // notice are pure (views::), so K3 verifies both off-screen.
        const Dimensions term = Terminal::Size();
        if (views::belowMinimumSize(term.dimx, term.dimy)) {
            return views::renderMinSizeNotice();
        }
        if (activeTab == 1) {
            const StatusRow status = statusRow();
            return views::renderModulesView({.activeTab = activeTab,
                                             .banner = bannerText,
                                             .filterInput = moduleFilterInput->Render(),
                                             .columnHeader = modulesVm.columnHeader(),
                                             .list = modulesMenu->Render(),
                                             .detail = moduleDetail->Render(),
                                             .statusText = status.text,
                                             .leftPaneWidth = widePaneWidth(),
                                             .bannerRole = modulesBannerRole,
                                             .statusRole = status.role,
                                             .bannerGlyph = daemonBannerGlyph,
                                             .diagnosticLines = daemonDiagnostics,
                                             .showDiagnostics = showDiagnostics,
                                             .terminalWidth = term.dimx},
                                            theme);
        }
        if (activeTab == 2) {
            const StatusRow status = updatesStatusRow();
            return views::renderUpdatesView({.activeTab = activeTab,
                                             .banner = bannerText,
                                             .requestBanner = updatesVm.requestBanner(),
                                             .columnHeader = updatesVm.columnHeader(),
                                             .list = updatesMenu->Render(),
                                             .detail = updatesDetail->Render(),
                                             .statusText = status.text,
                                             .leftPaneWidth = widePaneWidth(),
                                             .statusRole = status.role,
                                             .bannerRole = updatesBannerRole,
                                             .bannerGlyph = updatesBannerGlyph,
                                             .diagnosticLines = updatesDiagnostics,
                                             .showDiagnostics = showDiagnostics,
                                             .terminalWidth = term.dimx},
                                            theme);
        }
        if (activeTab == 3) {
            // The preview modal replaces the master-detail body; build only the
            // interactive renders the active branch needs so a hidden Menu is
            // never rendered off-screen (behaviour-preserving vs. the prior
            // if/else that did the same).
            const StatusRow status = statusRow();
            views::SnapshotsView v{.activeTab = activeTab,
                                   .banner = bannerText,
                                   .statusText = status.text,
                                   .leftPaneWidth = widePaneWidth(),
                                   .statusRole = status.role,
                                   .bannerRole = daemonBannerRole,
                                   .bannerGlyph = daemonBannerGlyph,
                                   .diagnosticLines = daemonDiagnostics,
                                   .showDiagnostics = showDiagnostics,
                                   .terminalWidth = term.dimx};
            if (preview) {
                v.showPreview = true;
                v.previewLines = snapshotsVm.previewLines();
            } else {
                v.filterInput = snapshotFilterInput->Render();
                v.list = snapshotsMenu->Render();
                v.detail = snapshotsDetail->Render();
                v.guidanceLines = snapshotsVm.restoreGuidanceLines();
            }
            return views::renderSnapshotsView(std::move(v), theme);
        }
        // Legend is a full-width row like the other two tabs (it used to live
        // inside the 44-column left pane, where it truncated mid-shortcut). The
        // whole Devices tab composition now lives in views::renderDevicesView;
        // the shell supplies the interactive component renders and the theme.
        const StatusRow status = statusRow();
        return views::renderDevicesView({.activeTab = activeTab,
                                         .filterInput = searchInput->Render(),
                                         .deviceList = deviceMenu->Render(),
                                         .detail = detailRenderer->Render(),
                                         .statusText = status.text,
                                         .leftPaneWidth = kLeftPaneWidth,
                                         .statusRole = status.role,
                                         .banner = daemonSentence,
                                         .bannerRole = daemonBannerRole,
                                         .bannerGlyph = daemonBannerGlyph,
                                         .diagnosticLines = daemonDiagnostics,
                                         .showDiagnostics = showDiagnostics,
                                         .terminalWidth = term.dimx},
                                        theme);
    });

    // Single tab-entry path shared by the 'm' cycle and the direct 1/2/3
    // keys, so the per-tab side effects (banner recompute, rebuild, updates
    // refresh) can never diverge between the two routes. Focus lands on the
    // tab's list: predictable, and it keeps single-key verbs live (the filter
    // guard below routes keys to a filter Input only while it owns focus).
    auto switchToTab = [&](int tab) {
        activeTab = tab;
        // Every tab the daemon feeds shows the same note, so it is recomputed on
        // every entry rather than per-tab — a user who never opens Snapshots
        // still learns the daemon is down.
        refreshDaemonNote();
        if (tab == 1) {
            // Text and valence together (design D6) — nothing downstream reads
            // the string to decide the colour.
            const auto line = modulesVm.bannerLine();
            bannerText = line.text;
            modulesBannerRole = roleForSeverity(line.severity);
            modulesVm.rebuild();
            modDetailDirty = true;       // A-1: fresh snapshot under the cache
            modulesVm.fillSignatures();  // cached names are skipped
            modulesMenu->TakeFocus();
        } else if (tab == 2) {
            refreshUpdatesBanner();
            updatesVm.rebuild();
            updDetailDirty = true;  // A-1 idiom: fresh snapshot under the cache
            prunePending();
            pending.push_back(facade.refreshUpdates());  // fresh data on entry
            updatesMenu->TakeFocus();
        } else if (tab == 3) {
            snapshotsVm.rebuild();
            refreshDaemonNote();  // after rebuild: the refresh it triggered may have changed it
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
        if (event == kRevealTick) {
            ++revealTick;
            return true;  // handled → FTXUI re-renders → the reveal advances one glyph
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
            if (activeTab == 2) refreshUpdatesBanner();
            if (activeTab == 3) bannerText = snapshotsVm.banner();
            // The daemon note is NOT tab-gated here, unlike the two above: it is
            // the same note on every tab the daemon feeds, and this drain is
            // where a completed refresh first reveals that the helper is not
            // answering. Without it the note only existed from the next tab
            // ENTRY onward, so the Devices tab a user starts on stayed silent
            // while its verbs sat dimmed. Reads BackendStatusVM state only — no
            // PAL query, so it is safe at drain frequency (unlike banner()).
            refreshDaemonNote();
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
        if (preview) {  // restore preview modal — explicit confirm only
            if (event == Event::Character('y')) {
                auto go = std::move(preview->onConfirm);
                preview.reset();
                go();
            } else if (event == Event::Character('n') || event == Event::Escape) {
                preview.reset();  // dismissed without restoring
            }
            return true;  // swallow everything else while open (DESIGN.md §8)
        }
        // While a filter Input owns focus, printable keys belong to the
        // filter, never to single-key commands (typing 'U' must not unbind a
        // driver mid-search). Enter hands focus back to the list keeping the
        // filter text; Escape also clears it (DESIGN.md §5.1 "a direct way to
        // clear the filter").
        const Component filterInput = activeTab == 0   ? searchInput
                                      : activeTab == 1 ? moduleFilterInput
                                      : activeTab == 3 ? snapshotFilterInput
                                                       : nullptr;
        if (filterInput && filterInput->Focused()) {
            const Component menu = activeTab == 0   ? deviceMenu
                                   : activeTab == 1 ? modulesMenu
                                                    : snapshotsMenu;
            // The routing decision is a pure function (K4), so the whole
            // command-key union is proved to reach the Input, not a command,
            // off-screen in test_filter_routing.cpp; here we only perform the
            // side effect it names.
            switch (nav::routeFilterKey(true, event)) {
                case nav::FilterKeyAction::HandBackToList:
                    menu->TakeFocus();
                    return true;
                case nav::FilterKeyAction::ClearAndHandBack:
                    if (activeTab == 0) {
                        filter.clear();
                        listVm.setFilter(filter);
                    } else if (activeTab == 1) {
                        moduleFilter.clear();
                        modulesVm.setFilter(moduleFilter);
                    } else {
                        snapshotFilter.clear();
                        snapshotsVm.setFilter(snapshotFilter);
                    }
                    menu->TakeFocus();
                    return true;
                case nav::FilterKeyAction::PassToInput:
                    return false;  // characters, backspace, arrows, mouse → the Input
                case nav::FilterKeyAction::NotFiltering:
                    break;  // unreachable: guarded by filterInput->Focused() above
            }
        }
        if (event == Event::Character('/') &&
            (activeTab == 0 || activeTab == 1 || activeTab == 3)) {  // updates has no filter
            (activeTab == 0   ? searchInput
             : activeTab == 1 ? moduleFilterInput
                              : snapshotFilterInput)
                ->TakeFocus();
            return true;
        }
        // Diagnostics reveal (design D4). Global like 'm' and the digits; 'd' was
        // the natural mnemonic but is already bound per-view. It acts only while
        // a backend is actually degraded — otherwise the key is inert and the
        // legend never advertises it, so it can never open an empty region.
        if (event == Event::Character('i') &&
            !(updatesDiagnostics.empty() && daemonDiagnostics.empty())) {
            showDiagnostics = !showDiagnostics;
            return true;
        }
        if (event == Event::Escape && showDiagnostics) {
            showDiagnostics = false;  // closes the reveal instead of quitting
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
            if (event == Event::Character('r')) {  // restore selected (preview modal)
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
                // Preview first, restore only on explicit confirm from it
                // (snapshot-ui spec). The diff fetch is async — the modal opens
                // immediately in its loading state and fills in when it lands.
                snapshotsVm.requestPreview(args->id);
                preview = PendingPreview{.onConfirm = [&, a = *args] {
                    prunePending();
                    pending.push_back(facade.restoreSnapshot(a.id));
                }};
                return true;
            }
            if (event == Event::Character('d')) {  // toggle diff against live state
                if (snapDiffView) {
                    snapDiffView = false;
                    snapDetailDirty = true;
                    return true;
                }
                const auto id = snapshotsVm.selectedSnapshotId();
                if (!id) {
                    bus.publish(
                        core::TaskCompletedEvent{.taskId = "guard",
                                                 .ok = false,
                                                 .message = "cannot diff: no snapshot selected"});
                    return true;
                }
                snapshotsVm.requestPreview(*id);
                // Remember which snapshot this diff describes: the selection
                // can move while the pane is open, and a diff silently
                // re-labelled to the newly selected row would be a lie.
                snapDiffForId = *id;
                snapDiffView = true;
                return true;
            }
            if (event == Event::Character('h')) {  // toggle parent-chain history
                snapshotsVm.setHistoryView(!snapshotsVm.historyView());
                snapDetailDirty = true;
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

    // Reveal ticker: wakes every 150 ms but posts (→ re-render) only while the
    // last render flagged a selected row whose reveal has NOT yet come to rest.
    // Once it rests — or the selection moves to a row that fits — the flag stays
    // false and the screen goes completely static (DESIGN.md §4.5; design
    // Decision 9). Joined on both exit paths below, before the screen and VM
    // locals unwind.
    std::atomic<bool> revealTickerRun{true};
    constexpr auto kRevealTickPeriod = std::chrono::milliseconds(150);  // DESIGN.md §4.5
    std::thread revealTicker([&] {
        while (revealTickerRun.load()) {
            std::this_thread::sleep_for(kRevealTickPeriod);
            if (revealTickerRun.load() && revealRunning.load()) screen.PostEvent(kRevealTick);
        }
    });
    auto stopRevealTicker = [&] {
        revealTickerRun = false;
        if (revealTicker.joinable()) revealTicker.join();
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
        // Startup lands on Devices WITHOUT going through switchToTab, and this
        // initial refresh is where an unreachable helper is first discovered —
        // so the note has to be computed here, after it. Without this the first
        // screen a user sees claimed nothing was wrong while every mutating
        // verb sat dimmed, and the note only appeared once they happened to
        // visit another tab and come back. The GUI shows it from its first
        // frame; this is what keeps the two surfaces saying the same thing at
        // the same time.
        refreshDaemonNote();
        statusVm.arm();
        if (auto started = hotplug.start(); !started) {
            // Degrade gracefully: without live events, 'r' refresh still works.
            bus.publish(core::ErrorEvent{.source = "hotplug", .message = started.error().message});
        }
        if (selfTest) {
            // Same line as the GUI's --self-test: wiring + one enumeration
            // proved, without ever entering the alternate screen.
            std::cout << "self-test rows: " << listVm.rowsRef().size() << "\n";
        } else {
            screen.Loop(root);
        }
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
        stopRevealTicker();  // ticker posts into `screen` — join before unwind
        hotplug.stop();
        delayed.shutdown();
        // Mirror the normal-path drain below: an enumeration still running on the
        // scheduler when the exception was thrown could otherwise publish into
        // listVm/detailVm/statusVm while they are being destroyed during stack
        // unwind. Wait for every submitted refresh to finish before rethrowing.
        drainPending(pending);
        throw;
    }

    // Stop event sources before the VMs/dispatcher unwind: join the reveal
    // ticker (posts into `screen`), then the monitor reader thread (no new
    // events into the debounce map), then the timer thread (no flush/clear
    // callback can still publish into a VM being torn down). Order:
    // ticker -> monitor -> timer -> drain in-flight refreshes.
    stopRevealTicker();
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
// NOLINTEND(readability-function-cognitive-complexity,readability-function-size)

}  // namespace devmgr::tui
