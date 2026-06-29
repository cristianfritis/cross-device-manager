# Phase 2 — Hotplug (live device updates) — Design

**Status:** Design outline. To be refined through its own brainstorm → research (Context7 for `udev_monitor`/netlink + umockdev uevent injection) → plan cycle before implementation. Builds directly on Phase 1.
**Milestone:** Plug/unplug a USB device and watch the TUI list update live — no `'r'` needed.

---

## Context

Phase 1 produces a static snapshot refreshed manually. Phase 2 makes the model **live**: an OS event source pushes add/remove/change events, they are debounced into stable domain events, and they flow through the *same* `DeviceService` → `EventBus` → ViewModel → `IUiDispatcher` path Phase 1 built. No new UI architecture — this phase proves the event pipeline end-to-end against a real, asynchronous source.

The PAL interface already exists from Phase 0: `pal::IHotplugMonitor { Result<void> start(Callback); void stop(); }` with `Callback = std::function<void(const HotplugEvent&)>` and `HotplugEvent` in `core/include/devmgr/pal/hotplug_event.hpp`.

---

## Components (proposed)

### `UdevHotplugMonitor` (implements `pal::IHotplugMonitor`, in `devmgr_pal_linux`)
- Wraps a netlink `udev_monitor`. `start()` creates the monitor (`udev_monitor_new_from_netlink(udev, "udev")`), filters to the modeled subsystems, enables receiving, and runs a dedicated reader thread polling `udev_monitor_get_fd()` and calling `udev_monitor_receive_device()`, translating each to a `HotplugEvent` and invoking the callback. `stop()` signals the thread (eventfd/self-pipe) and joins.
- **APIs to verify via Context7 at kickoff:** `udev_monitor_new_from_netlink`, `udev_monitor_filter_add_match_subsystem_devtype`, `udev_monitor_enable_receiving`, `udev_monitor_get_fd`, `udev_monitor_receive_device`, `udev_device_get_action` (`add`/`remove`/`change`/`bind`/`unbind`).

### `HotplugService` (in `devmgr_app`)
- Wraps `IHotplugMonitor`. **Debounces per-device** over a short window (target ~250 ms, configurable) to coalesce rapid plug/unplug churn into one stable event, then drives `DeviceService`:
  - `add`/`change` → targeted re-read of that device (preferred) or a scoped re-enumerate → feed `DeviceService` so it emits the right `DeviceAdded/Changed`.
  - `remove` → `DeviceService` drops the id and emits `DeviceRemovedEvent`.
- The debounce timer lives on the `TaskScheduler` / a timer utility, never on the UI thread.

### `DeviceService` (extended)
- Add an incremental apply path (e.g. `applyHotplugDelta`) alongside Phase 1's `applyEnumeration`, reusing the same diff/equality logic so a re-read of one device still produces a precise `Changed` (or no-op) rather than churn.

### TUI (no architectural change)
- The list updates live via the existing Added/Removed/Changed → `IUiDispatcher` path. Optional: a transient status line ("usb device added") and preserve selection across updates by `DeviceId` (not index). `'r'` manual refresh stays as a fallback.

---

## Testing & verification (proposed)

- **Unit (FakePal-style, no native deps):** a `FakeHotplugMonitor` implementing `IHotplugMonitor` lets tests inject synthetic events. Assert: debounce coalesces a burst into one event; add→change→remove sequences produce correct domain events; rapid add/remove within the window cancels cleanly; `stop()` joins without deadlock; selection-by-id survives a list mutation.
- **Integration (container, gated):** extend the umockdev rig — `umockdev_testbed_uevent()` (verify exact API at kickoff) to emit add/remove against a seeded device and assert the real `UdevHotplugMonitor` delivers a matching `HotplugEvent`.
- **Manual smoke (real host):** run `devmgr-tui`, plug/unplug a spare USB device, watch the list update live; cross-check with `udevadm monitor`.

## Key risks (proposed)

- **Debounce window tuning** — too short = churn/flicker; too long = sluggish UX.
- **Clean shutdown** — the reader thread must wake and join on `stop()`/exit (use an eventfd/self-pipe, not a blocking read with no wakeup).
- **Event/enumeration race** — a hotplug delta arriving during/just after a full refresh must not produce duplicate or lost devices; reconcile by `DeviceId` keeps this idempotent.
- **Netlink buffer overflow** under event storms — set an adequate receive buffer; coalesce.
- **Thread-safety** — same rule as Phase 1: all UI/VM-state mutation marshaled through `IUiDispatcher`.

## Out of scope

- Any device mutation (Phase 4). Qt GUI hotplug parity is validated in **Phase 3** (it reuses this same pipeline).
