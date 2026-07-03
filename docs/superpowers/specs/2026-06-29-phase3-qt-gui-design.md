# Phase 3 — Qt 6 GUI (read + hotplug parity) — Design

**Status:** DESIGNED — brainstormed and approved 2026-07-02. Next: implementation plan (`docs/superpowers/plans/`).
**Milestone:** A Qt 6 desktop GUI showing the **same** device data and **live hotplug** as the TUI — proving two frontends share one core. This is the validation of the UI-abstraction.
**Builds on:** Phases 1–2 (`feature/phase3` branch, forked from the committed Phase 2 work).

---

## Decisions locked (brainstorm 2026-07-02)

| Decision | Choice | Rationale |
|---|---|---|
| GUI scope | **Strict TUI parity** — grouped list, detail pane, filter, refresh, live hotplug, transient status | Keeps the phase focused on proving the one-core/two-frontends abstraction; richer GUI is Phase 3.x+ |
| Qt flavor | **Qt Widgets** (`QListView` + model/view) | Dense system-tool UI; mature model/view; easy offscreen testing; no QML runtime |
| Qt provisioning | **System packages** — `pacman qt6-base` locally, `apt qt6-base-dev` in CI/Docker | Dynamic link satisfies LGPLv3; vcpkg Qt is a multi-hour build for zero benefit |
| CI posture | **Build + QTest + offscreen smoke** | `qt6-base-dev` adds ~1–2 min to CI; `QAbstractItemModelTester` catches model bugs where they actually happen |
| Data path | **Approach A: flat `QAbstractListModel` over `DeviceListVM`** | Maximum reuse — grouping/filter/selection logic runs once, in the shared VM. Rejected: (B) structured-rows refactor + `QTreeView` (devmgr_app churn beyond parity scope); (C) Qt-native model on the facade (duplicates VM logic; abandons the abstraction-validation goal) |
| Minimum Qt | **Qt ≥ 6.4** (Ubuntu 24.04 CI floor; CachyOS ships newer) | Constrains API choices — see `QtUiDispatcher` note on `invokeMethod` overloads |

---

## Context

Phases 1–2 built the OS-agnostic core, services, `ApplicationFacade`, ViewModels (`DeviceListVM`, `DeviceDetailVM`, `StatusLineVM`), `EventBus`, and the `IUiDispatcher` seam. Phase 3 adds a **second** frontend that reuses all of it. The only new code lives in `gui/`; the only `devmgr_app` change is two small toolkit-agnostic additions to `DeviceListVM` (below). If GUI work tempts a Qt dependency inside `app/`, the seam is wrong — that is a design failure, not an inconvenience.

Key property exploited by the design: **all `DeviceListVM` row mutation funnels through `rebuild()`** — filter keystrokes call it directly on the UI thread; hotplug deltas arrive as one coalesced dispatcher post that calls it. One pair of hooks around `rebuild()` therefore covers every Qt model-reset case.

---

## Components

### `gui/` — new top-level directory (mirrors `tui/`)

`devmgr-gui` executable, Linux-only. Links `devmgr_app + devmgr_core + devmgr_pal_linux + Qt6::Widgets`. CMake: `find_package(Qt6 COMPONENTS Widgets Test QUIET)` behind an option (default ON when Qt6 is found on `UNIX AND NOT APPLE`); the tree must still configure and build with no Qt installed. `CMAKE_AUTOMOC` enabled only for gui targets.

### `QtUiDispatcher : app::IUiDispatcher`

- Owns (or is handed) a plain `QObject` context living on the GUI thread.
- `post(fn)` = `QMetaObject::invokeMethod(context_, std::move(fn))` — the **auto-connection functor overload** (Qt ≥ 5.10). Called from a background thread it queues onto the context's thread; called on the GUI thread it runs directly, matching the FTXUI dispatcher's semantics.
- **Do not** use the functor + explicit `Qt::ConnectionType` overload — it is **Qt 6.7+** (verified via Context7 2026-07-02) and would break the Ubuntu 24.04 CI floor.
- Teardown property (document in the header, mirroring the `DelayedScheduler` doc-comments): once the context `QObject` is destroyed, undelivered posts are dropped silently. Teardown ordering (below) must therefore stop producers before destroying the context, exactly like the TUI.

### `DeviceListModel : QAbstractListModel`

Thin adapter (~100 LoC) over `DeviceListVM`:
- `rowCount()` / `data(DisplayRole)` serve the VM's existing row strings (`rowsRef()`).
- `flags()`: header rows (`vm.isHeader(row)`) → not selectable (rendered bold/dim via a custom role or font in `data()`); device rows → `Qt::ItemIsEnabled | Qt::ItemIsSelectable`.
- Registers the VM's before/after rebuild hooks: before → `beginResetModel()`, after → `endResetModel()` + re-apply selection (below).
- No storage of its own — the VM's `rows_`/`rowIds_` remain the single source of truth.

### `devmgr_app` changes — the seam test (toolkit-agnostic, no Qt includes)

1. `bool DeviceListVM::isHeader(int row) const` — exposes what `rowIds_` already encodes (`nullopt` == group header).
2. Optional rebuild hooks: `setRebuildHooks(std::function<void()> before, std::function<void()> after)`, invoked at entry/exit of `rebuild()` on the UI thread. Unset hooks are no-ops. The TUI does not use them — **zero TUI changes this phase**.

Covered by plain (non-Qt) unit tests in `app/` with the existing FakeDispatcher pattern.

### Main window (`QMainWindow`)

- Toolbar: **Refresh** action → same `std::async` + drain-pending-futures pattern `runTuiApp()` uses (prune completed futures on each trigger; drain all before teardown). The GUI thread never touches udev/sysfs.
- `QLineEdit` filter → `vm.setFilter(text)` on every edit (VM rebuild + model reset per keystroke is fine — Phase 2.5 made rebuilds allocation-cheap).
- `QSplitter`: `QListView` (device list) | detail pane — a read-only two-column key/value `QTreeWidget` — filled from `DeviceDetailVM` on selection change.
- `QStatusBar` shows `StatusLineVM` text (transient hotplug messages; its `DelayedScheduler` expiry semantics are unchanged).
- `main.cpp` wiring mirrors `runTuiApp()`: PAL → facade → services → hotplug → VMs, with `QtUiDispatcher` in place of the FTXUI one.

### Selection sync (one source of truth: the VM)

The VM already preserves selection by `DeviceId` across rebuilds and clamps it. Qt's `QItemSelectionModel` is a **mirror**:
- View `selectionChanged` → write `vm.selectedRef()` → refresh detail pane from `vm.selectedDeviceId()`.
- After each model reset (after-hook) → re-apply `vm.selectedRef()` to the view's current index.

---

## Data flow & threading

```
netlink thread ─▶ HotplugService (debounce) ─▶ DeviceService::applyDelta ─▶ EventBus
                                                                              │
DeviceListVM::onModelChanged (publisher thread): invalidate snapshot,        │
one coalesced dispatcher.post ◀───────────────────────────────────────────────┘
                    │
GUI thread: before-hook (beginResetModel) → rebuild() → after-hook
            (endResetModel + reselect) → QListView re-queries
```

Rules (identical to the TUI's):
- Only the GUI thread touches widgets and VM row state; all cross-thread marshaling goes through `QtUiDispatcher`.
- Enumeration/refresh I/O runs off-thread (`std::async` futures, pruned and drained as in `tui_app.cpp`).

## Teardown & error handling

Mirror `runTuiApp()`'s hard-won ordering, exception-safe (the Phase 2 `drainPending` pattern):
1. `app.exec()` returns (or throws) →
2. stop `HotplugService` (Phase 2 claim-at-schedule-time semantics hold),
3. drain pending refresh futures,
4. destroy VMs,
5. destroy `QtUiDispatcher` context last among the wiring.

The `DelayedScheduler` lifetime preconditions documented in Phase 2 apply unchanged. The known `EventBus::publish()` unsubscribe-race residual carries over with the same mitigation (this teardown ordering); no new exposure is added by the GUI.

---

## Testing & verification

- **Qt unit tests (`gui/tests/`, offscreen):** GTest-harnessed with a `QApplication` main — one test framework repo-wide — linking `Qt6::Test` for `QAbstractItemModelTester`/`QSignalSpy`.
  - `DeviceListModel` under `QAbstractItemModelTester` (`FailureReportingMode::Fatal`, since we are not running inside the QtTest framework) while driving VM rebuilds/filters — auto-validates the model-invariant surface.
  - Targeted: header rows non-selectable, role data matches VM rows, reset fires per rebuild, selection re-applied after reset.
  - `QtUiDispatcher`: post from a `std::thread`, assert execution on the GUI thread; posts after context destruction are dropped.
- **Non-Qt units (`app/`):** `isHeader()` + rebuild-hook behavior with FakeDispatcher. Existing 54 tests untouched.
- **Offscreen smoke (CI):** `devmgr-gui --self-test` — construct full wiring, enumerate, one rebuild, print row count, exit 0. Run with `QT_QPA_PLATFORM=offscreen`.
- **Purity guard (CI):** grep gate asserting no Qt include (`#include <Q`, `#include "Q`) appears under `app/`, `core/`, or `platform/`.
- **Manual parity smoke (user, real host — gated):** run `devmgr-tui` and `devmgr-gui` side by side: identical device data/counts/grouping; USB plug/unplug updates **both** live (≤ debounce window) with transient status shown; filter behaves identically; GUI never freezes during enumeration; clean exit both.

## Key risks

- **Qt version skew** — CachyOS ships a much newer Qt than Ubuntu 24.04 CI (~6.4). Target only Qt ≥ 6.4 APIs; CI building against the floor catches accidental newer-API usage.
- **Qt threading** — only the GUI thread may touch widgets; `QtUiDispatcher` is mandatory and its drop-after-context-destruction semantics must be honored by teardown ordering.
- **VM purity** — a Qt leak into `devmgr_app` breaks the one-core/two-frontends guarantee; enforced by the CI grep gate, not just review.
- **Reset-based updates clear view selection** — mitigated by VM-as-source-of-truth reselection in the after-hook; covered by a QTest unit.
- **Docker/CI image growth** — `qt6-base-dev` adds a few hundred MB to the test image; accepted cost of the chosen CI posture.

## Out of scope

- Any device mutation (Phase 4). QML/Qt Quick. Cross-platform GUI (Windows/macOS — Phase 9). Tray icon, `.desktop` packaging, richer-than-TUI GUI features (Phase 3.x+ candidates: structured `QTreeView` via a structured-rows VM API, multi-column table, icons).
