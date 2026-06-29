# Phase 3 — Qt 6 GUI (read + hotplug parity) — Design

**Status:** Design outline. To be refined through its own brainstorm → research (Context7 for Qt 6 model/view + threading) → plan cycle before implementation. Builds on Phases 1–2.
**Milestone:** A Qt 6 desktop GUI showing the **same** device data and **live hotplug** as the TUI — proving two frontends share one core. This is the validation of the UI-abstraction.

---

## Context

Phases 1–2 built the OS-agnostic core, services, `ApplicationFacade`, ViewModels, `EventBus`, and the `IUiDispatcher` seam. Phase 3 adds a **second** frontend that reuses all of it unchanged. The only genuinely new code is (a) a `QtUiDispatcher` implementing `IUiDispatcher`, and (b) Qt view adapters over the existing ViewModels. If the GUI can be built almost entirely from the shared layer, the abstraction is proven correct; anything that forces a change to `devmgr_app` is a design smell to fix.

---

## Components (proposed)

### `devmgr-gui` (executable, Linux-only for now)
- Links `devmgr_app + devmgr_core + devmgr_pal_linux + Qt6::Widgets`. Qt 6 from system packages (LGPLv3, dynamic link). `find_package(Qt6 COMPONENTS Widgets REQUIRED)`. Guarded behind `if(UNIX AND NOT APPLE)` + an availability check so the rest of the tree still builds without Qt.

### `QtUiDispatcher` (implements `app::IUiDispatcher`)
- `post(fn)` marshals the closure onto the Qt main (GUI) thread via `QMetaObject::invokeMethod(receiver, std::move(fn), Qt::QueuedConnection)`. This is the GUI side of the same seam the TUI implements with `PostEvent`.

### Qt view adapters (the only other new code)
- A `QAbstractItemModel` adapter (tree grouped by bus, or table) fed by `DeviceListVM`; selection → `DeviceDetailVM` drives a property pane (`QTreeWidget`/form). A toolbar **Refresh** action calls `facade.refresh()`; a search `QLineEdit` drives `vm.setFilter` (or a `QSortFilterProxyModel`); grouping/sort by bus mirrors the VM ordering. Background work stays on `TaskScheduler`; the GUI thread only renders — never does I/O.
- **APIs to verify via Context7 at kickoff:** `QAbstractItemModel`/`beginInsertRows` et al., `QSortFilterProxyModel`, queued-connection `QMetaObject::invokeMethod` overloads, `QThreadPool`, and `QT_QPA_PLATFORM=offscreen` for headless smoke.

### `devmgr_app` (unchanged — the test of the abstraction)
- ViewModels must remain toolkit-agnostic: **no Qt include** may appear in `devmgr_app`. If GUI work tempts a Qt dependency there, the seam is wrong.

---

## Testing & verification (proposed)

- **Unit:** any non-trivial Qt model-adapter logic tested with `QTest` where feasible; the row/detail/filter logic itself is already covered by the FakePal VM tests from Phase 1 and is reused as-is.
- **Headless smoke (container/CI):** build the GUI and run a minimal launch under `QT_QPA_PLATFORM=offscreen` to catch wiring/regressions without a display. (Qt is heavy in CI — likely build-only by default, with the offscreen smoke optional.)
- **Manual parity smoke (real host):** launch `devmgr-tui` and `devmgr-gui` side by side; confirm identical device data and counts, that hotplug updates both live, that the GUI never freezes during enumeration, and that search/grouping/selection behave the same.

## Key risks (proposed)

- **Qt threading** — only the GUI thread may touch widgets; the `QtUiDispatcher` is mandatory and must be correct, or random crashes.
- **VM purity** — keep `devmgr_app` Qt-free; a leak there breaks the "one core, two frontends" guarantee.
- **CI weight & packaging** — Qt 6 inflates the container/build; decide build-only vs offscreen-smoke in CI, and how Qt is provisioned (system apt vs vcpkg).
- **Model/view mapping** — grouped/header rows and selection-by-`DeviceId` must map cleanly onto `QAbstractItemModel` indices; reuse the VM's ordering rather than re-deriving it.

## Out of scope

- Any device mutation (Phase 4). Cross-platform GUI (Windows/macOS) is Phase 9 (future).
