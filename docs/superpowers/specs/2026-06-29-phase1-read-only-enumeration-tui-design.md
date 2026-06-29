# Phase 1 — Read-only Enumeration + first FTXUI TUI — Design

**Status:** Approved, ready for implementation planning.
**Milestone:** Launch `devmgr-tui` on a Linux machine and see your **real** devices — grouped/sorted by bus, searchable, with a detail pane, manual refresh, and mouse support.

> Library APIs in this spec were verified on 2026-06-29 against the actual toolchain: FTXUI **6.1.9** (local vcpkg port), libudev **v260** (`/usr/include/libudev.h`), and umockdev (`umockdev-1.0`, already in the project Dockerfile).

---

## Context

Phase 0 delivered the OS-agnostic foundation: the `Device`/`Driver` models, `Result/Error`, a thread-safe `EventBus`, `TaskScheduler`, logging, the PAL **interfaces** (`IDeviceEnumerator`, …), and an in-memory `FakePal` test double — all in the static lib `devmgr_core` with a GoogleTest unit suite, Docker/VM/CI rigs.

Phase 1 is the first **vertical slice** that produces something a user runs. It introduces the first *real* Linux PAL implementation (`UdevDeviceEnumerator`), the service + application layers that turn enumeration into a live model, the UI-abstraction (`IUiDispatcher` + ViewModels), and the first frontend (FTXUI). It deliberately also exercises `IUiDispatcher` end-to-end — the single seam that will let the Phase 3 Qt GUI reuse the exact same core. Getting that seam right now is why the TUI marshals all work through `TaskScheduler` → `EventBus` → `IUiDispatcher` rather than enumerating inline.

Out of scope for Phase 1: live hotplug updates (Phase 2), any device mutation/enable-disable (Phase 4). Refresh is manual (`'r'`) here.

---

## Locked decisions inherited (do not re-litigate)

- **Linux-first**, core stays OS-agnostic; libudev lives only in a separate platform target.
- **TUI = FTXUI**; the GUI (Phase 3) = Qt 6. Both link the same `devmgr_core` + app layer and talk only to `ApplicationFacade` + `EventBus`.
- **UI marshaling via `IUiDispatcher`** — TUI side = `ScreenInteractive::PostEvent(Event::Custom)` draining a thread-safe queue on the UI thread.
- **Every long op runs on `TaskScheduler`** — the UI thread never does I/O.
- **Testing isolation** — unit tests use `FakePal` (no native deps); the real udev path is umockdev-tested **inside the container only** (never touches the host `/sys`).

## Decisions resolved in this design

- **Namespace & path:** `devmgr::platform_linux` under `platform/linux/`, headers at `platform/linux/include/devmgr/platform/linux/…`. (Avoids GCC's predefined `linux` macro; keeps namespace and include path consistent.)
- **Dispatcher mechanism:** the locked `PostEvent(Event::Custom)` + drain-queue design. FTXUI 6.1.9 also offers `ScreenInteractive::Post(Closure)` (runs the closure on the UI thread directly); it's noted as a cleaner alternative but the queue variant is the canonical choice and is portability-safe.
- **`DeviceId` derivation:** deterministic **FNV-1a-64** over `subsystem \x1f syspath \x1f vendor:product \x1f serial`, formatted `dev-%016x`. `std::hash<std::string>` is *not* stable across processes and would break reconcile + Phase 7 snapshot matching.
- **Enumeration scope:** filter to the modeled subsystems `pci/usb/platform/virtio` for the MVP list (no filter ⇒ thousands of `block/net/tty/...` nodes).

---

## Architecture & data flow

```
UdevDeviceEnumerator (libudev)  ──enumerate()──▶ Result<vector<Device>>
        │  (submitted to runtime::TaskScheduler — UI thread never blocks on I/O)
        ▼
ApplicationFacade::refresh()  ──▶  DeviceService::applyEnumeration(snapshot)
        │                              diff by DeviceId
        │                              ├─ in snapshot only      → publish DeviceAddedEvent
        │                              ├─ in model only         → publish DeviceRemovedEvent
        │                              ├─ in both, content diff  → publish DeviceChangedEvent
        │                              └─ unchanged             → (no event)   [idempotent]
        ▼  (EventBus — handlers fire OUTSIDE the bus lock, possibly on a worker thread)
DeviceListVM / DeviceDetailVM   ──IUiDispatcher::post(closure)──▶ mutate VM state on UI thread
        ▼
devmgr-tui (FTXUI)  +  FtxuiUiDispatcher (PostEvent(Event::Custom) → drain on UI thread → redraw)
```

On enumeration failure the facade publishes `ErrorEvent` and leaves the prior model intact.

### Target graph (keeps `devmgr_core` OS- and UI-agnostic)

| Target | Kind | Links | Holds |
|---|---|---|---|
| `devmgr_core` | static lib *(existing)* | spdlog, nlohmann-json, tl-expected, Threads | + field-wise `Device operator==` (new) |
| `devmgr_pal_linux` | static lib, **Linux-only** | `PUBLIC devmgr_core`, **`PRIVATE PkgConfig::UDEV`** | `UdevDeviceEnumerator` |
| `devmgr_app` | static lib, OS/UI-agnostic | `PUBLIC devmgr_core` *(no ftxui, no libudev)* | `DeviceService`, `ApplicationFacade`, `IUiDispatcher`, `DeviceListVM`, `DeviceDetailVM` |
| `devmgr-tui` | executable, Linux-only | `devmgr_app + devmgr_core + devmgr_pal_linux + ftxui::component/dom/screen` | FTXUI frontend, `FtxuiUiDispatcher` |
| `devmgr_tests` | test exe *(existing, extended)* | `devmgr_core + devmgr_app + GTest` — **no native deps** | unit tests (FakePal + inline dispatcher) |
| `devmgr_integration_tests` | test exe, **gated** (`DEVMGR_BUILD_INTEGRATION_TESTS`, auto-on iff umockdev present) | `devmgr_pal_linux + PkgConfig::UMOCKDEV + GTest` | real-udev test, run under `umockdev-wrapper` |

libudev is **PRIVATE** in `devmgr_pal_linux` so it never leaks into `devmgr_core`/`devmgr_app`; the unit suite therefore builds and runs on a bare host with no native deps. Root `CMakeLists.txt` guards `platform/linux` and `tui` behind `if(UNIX AND NOT APPLE)` so the tree still configures on other platforms later.

---

## Components

### `UdevDeviceEnumerator` (implements existing `pal::IDeviceEnumerator`)
- `enumerate()`: fresh `udev_new()` per call (the context is not thread-safe; one-context-per-call is naturally safe since enumeration runs on the scheduler). Strict RAII deleters for `udev`/`udev_enumerate`/`udev_device`. `add_match_subsystem` for `pci/usb/platform/virtio`, `scan_devices`, then per list entry (`name` == syspath) `udev_device_new_from_syspath` → `mapDevice()`.
- **Fault isolation:** a failed `udev_new`/`enumerate_new`/`scan` → `makeError(Io)`; a *single* failed `new_from_syspath` → push one synthetic `Device{status=Error, errorNote=…}` and continue (never abort the scan).
- **Borrowed pointer caution:** `udev_device_get_parent()` returns a borrowed pointer auto-freed with the child — never `unref` it, never wrap it in `unique_ptr`.

**Field mapping (`udev_device*` → `core::Device`):**

| Field | Source (priority order) |
|---|---|
| `id` | `stableId()` = FNV-1a-64 of `subsystem \x1f syspath \x1f vendor:product \x1f serial` → `dev-%016x` |
| `bus` | subsystem → `Pci/Usb/Platform/Virtio/Other` |
| `name` | `ID_MODEL_FROM_DATABASE` → `ID_MODEL` → sysattr `product` → sysname (never empty) |
| `sysfsPath` | `udev_device_get_syspath` |
| `modalias` | prop `MODALIAS` → sysattr `modalias` |
| `vendorId` | `ID_VENDOR_ID` → sysattr `idVendor` (usb) → sysattr `vendor` (pci, strip `0x`) |
| `productId` | `ID_MODEL_ID` → sysattr `idProduct` (usb) → sysattr `device` (pci, strip `0x`) |
| `serial` | `ID_SERIAL_SHORT` → `ID_SERIAL` (often empty, e.g. PCI) |
| `boundDriver` | `udev_device_get_driver` (NULL → `nullopt`) |
| `parent` | parent's syspath → **same** `stableId()` recipe (borrowed ptr) |
| `properties` | iterate `udev_device_get_properties_list_entry` → `map` |
| `status` | `Active` on success; `Error` + `errorNote` only on the per-device failure path |

The parent id **must** be computed with the same `stableId` helper as the parent's own id, or parent links silently break.

### `DeviceService` (in `devmgr_app`)
- Owns the in-memory model (`map<DeviceId,Device>` behind a mutex) and a reference to `EventBus`.
- `applyEnumeration(vector<Device> snapshot)` diffs by `DeviceId` and publishes `DeviceAddedEvent` / `DeviceRemovedEvent` / `DeviceChangedEvent`; unchanged ids → no event (idempotent re-apply emits nothing).
- Thread-safe `devices()` / `findById()` read accessors.
- Publishes **after** updating the model and **without** holding the service mutex (EventBus invokes handlers outside its own lock; a handler may call back into `devices()`).
- Requires a field-wise `core::operator==(const Device&, const Device&)` (added to `devmgr_core`), covering optionals and the (ordered `std::map`) `properties`. Too-loose comparison drops real `Changed` events; too-strict spams them.

### `IUiDispatcher` + `ApplicationFacade` (in `devmgr_app`)
- `IUiDispatcher` = pure interface `virtual void post(std::function<void()>) = 0;` — keeps the app layer UI-toolkit-agnostic.
- `ApplicationFacade` composes `IDeviceEnumerator&`, `TaskScheduler&`, `EventBus&`, `DeviceService`. `refresh()` submits `enumerate()` to the scheduler; on the worker, success → `applyEnumeration`, failure → publish `ErrorEvent`. Read API `devices()/findById()` proxies the service. `refresh()` returns promptly — it must never block the caller on enumeration, and the UI must never `future.get()` on the UI thread.
- A `tests/fakes/inline_ui_dispatcher.hpp` test double runs the posted closure synchronously for deterministic unit tests.

### `DeviceListVM` / `DeviceDetailVM` (in `devmgr_app`)
- `DeviceListVM` subscribes to Added/Removed/Changed; each handler marshals through `IUiDispatcher::post(...)` a closure that (on the UI thread) re-reads `facade.devices()` and rebuilds an ordered row list: **grouped/sorted by `BusType`** (stable secondary sort by name), optional bus group-header rows, **case-insensitive filter** by the current search string. Exposes the display-string vector for the FTXUI `Menu`, a `selected` index, `setFilter()`, and `selectedDeviceId()` (clamps/resets selection when the filtered list shrinks — the detail pane must never dereference out of range).
- `DeviceDetailVM` renders the selected `Device` into labeled lines (id, bus, name, sysfsPath, vendor:product, serial, driver, status, parent, key properties); tolerates empty selection.

### `devmgr-tui` + `FtxuiUiDispatcher` (in `tui/`)
- `FtxuiUiDispatcher::post(fn)` pushes onto a mutex-guarded queue then calls `screen.PostEvent(Event::Custom)`; the root `CatchEvent` drains the queue on `Event::Custom` (UI thread) and FTXUI redraws.
- `main()` wires `EventBus`, `TaskScheduler`, `UdevDeviceEnumerator`, `DeviceService`, `ApplicationFacade`, the VMs, the dispatcher, and `ScreenInteractive::Fullscreen()`.
- Layout: `Container::Vertical(search Input, device Menu)` on the left, piped `| vscroll_indicator | frame | flex`; a `separator()`; the detail `Element` on the right (`ResizableSplitLeft` or a `Renderer` hbox).
- Keys via `CatchEvent`: `'q'`/Esc → `screen.Exit()`; `'r'` → `facade.refresh()`; otherwise let `Menu`/`Input` handle it. Search `Input.on_change` → `vm.setFilter`; `Menu.on_change` → detail reads `vm.selectedDeviceId()`. Mouse is on by default (wheel scroll, click select, click-to-focus). Kick an initial `refresh()` at startup so the list populates without pressing `'r'`.
- TUI glue stays thin — all row/detail logic lives in the FakePal-tested VMs.

---

## Ordered tasks (SDD: one task per session, TDD, user commits each)

1. **Build graph + dependency wiring.** Add `ftxui` to `vcpkg.json`; create `platform/linux/`, `app/`, `tui/`, `tests/integration/` (each a CMakeLists + minimal compilable stub); wire root (always `app`; guard `platform/linux` + `tui` behind `if(UNIX AND NOT APPLE)`; QUIET-detect umockdev and set `option(DEVMGR_BUILD_INTEGRATION_TESTS … ${UMOCKDEV_FOUND})`; add `tests/integration` only when on). Extend CI clang-format / clang-tidy globs to `app platform tui`. *Verify: configures (resolves ftxui), builds all targets, existing unit suite green; `devmgr_tests` links no libudev/umockdev/ftxui; integration auto-off on bare host, on in container.*
2. **`UdevDeviceEnumerator` + umockdev integration test.** Implement per the recipe above. *Verify (container-only, gated): seed a umockdev testbed with a usb (`1d6b:0002`, MODALIAS, SUBSYSTEM=usb) + a pci device, call the real enumerator; assert field mapping, bus types, `0x`-stripped pci ids, parent-id == parent's own id, determinism across two calls, and one malformed entry → exactly one Error device. Runs under `umockdev-wrapper`; must not be runnable on a bare host.*
3. **`DeviceService` reconcile + events** (+ `Device operator==` in core). *Verify (FakePal unit): 2 seeded → exactly 2 Added; re-apply identical → 0 events; add/remove/mutate-one-field → 1 Added + 1 Removed + 1 Changed; equality false on any single differing field incl. `properties`/optionals; no deadlock when a handler reads `devices()` during publish.*
4. **`IUiDispatcher` + `ApplicationFacade` read API** (+ inline dispatcher double). *Verify (FakePal unit): seed N → `refresh()` → N Added and `devices()==N`; second refresh after mutation → correct deltas; enumerator returning `makeError(Io)` → one `ErrorEvent`, model unchanged; work observed on a scheduler worker, not the caller thread.*
5. **`DeviceListVM` + `DeviceDetailVM`.** *Verify (FakePal + inline dispatcher unit): rows grouped/sorted by bus deterministically; row↔DeviceId mapping correct; `setFilter` narrows case-insensitively and clamps selection; Added/Removed updates rows only after the dispatcher drains (state mutated inside the posted closure, never on the handler thread); detail lines correct + safe empty-selection placeholder.*
6. **FTXUI `devmgr-tui` + `FtxuiUiDispatcher`.** Thin glue over the tested VMs; initial refresh at startup. *Verify: dispatcher posting from a worker enqueues + triggers a Custom event and runs the closure on the UI thread (TSan clean); primary verification is the manual TUI smoke below.*

---

## Testing & verification

- **Unit suite (bare host, no native deps):** `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure` — all new + Phase 0 tests pass; confirm `devmgr_tests` links neither libudev, umockdev, nor ftxui.
- **Gating check:** on the host (no umockdev) `DEVMGR_BUILD_INTEGRATION_TESTS=OFF` and `tests/integration` is skipped; configure/build/unit cycle still succeeds.
- **umockdev integration (container; user runs):** `podman-compose -f test/docker-compose.yml run --rm unit` (CI uses `docker compose`). Image has `libudev-dev + umockdev + libumockdev-dev`; integration option auto-on; ctest runs `devmgr_integration` via `umockdev-wrapper`; seeded devices map correctly; the test cannot pass without the preload.
- **clang-format / clang-tidy** clean over the **extended** globs (`core tests app platform tui`).
- **Manual TUI smoke (real Linux host):** launch `devmgr-tui`; list auto-populates and is grouped/sorted by bus; arrow keys + mouse wheel scroll; click/Enter selects and updates the detail pane; search filters case-insensitively with selection staying valid; `'r'` re-enumerates; `'q'`/Esc exits cleanly leaving the terminal sane.
- **Ground-truth cross-check:** compare TUI device names/counts against `lspci`, `lsusb`, `udevadm info --export-db`; spot-check vendor:product and bound driver.
- **(Recommended) ThreadSanitizer** over the EventBus→dispatcher VM tests and `FtxuiUiDispatcher` worker posting.

---

## Key risks

- **umockdev-on-host danger** — the real enumerator reads host `/sys` without the preload; the integration target must stay gated (auto-off when umockdev absent) and run only in the container under `umockdev-wrapper`. Never force-enable it on a bare host; never dlopen the preload mid-run.
- **FTXUI thread-safety** — EventBus handlers run outside the bus lock, often on a worker; **all** FTXUI/VM-UI-state mutation must go through `IUiDispatcher`. Touching `ScreenInteractive` from a worker is a data race.
- **Stable `DeviceId`** — use the FNV-1a recipe; compute parent id with the same helper or links break and reconcile churns.
- **Enumeration fault isolation** — one bad device must not blank the whole list.
- **Borrowed `udev_device_get_parent`** — never unref / never `unique_ptr` (double-free).
- **libudev leak** — keep it `PRIVATE` in `devmgr_pal_linux`; if it leaks, core stops being OS-agnostic and the bare-host unit build breaks.
- **Selection clamp** — filter/refresh shrinking the list can dangle the Menu `selected` index.
- **vcpkg ftxui resolution** — manifest has no `builtin-baseline`; if the toolchain baseline drifts, pin 6.1.9 via `overrides`. Imported targets are `ftxui::screen/dom/component`.
- **Agent cannot validate the udev/container/TUI paths** — no Docker daemon in-agent and no real hardware; those steps are the user's to run (consistent with the Phase 0 cadence), and the agent cannot commit (user commits each task).

---

## Out of scope (deferred)

- Live hotplug / auto-refresh → **Phase 2**.
- Any device mutation (enable/disable), privileged helper → **Phase 4**.
- Qt GUI → **Phase 3**.
