# Phase 2 — Hotplug (live device updates) — Design

**Status:** Approved design (refined 2026-06-30 from the original outline through brainstorm). Ready for the Context7-research → implementation-plan cycle. Builds directly on Phase 1.
**Milestone:** Plug/unplug a USB device and watch the `devmgr-tui` list update **live** — no `'r'` needed — with the selected device staying put and a transient "usb device added" status line.

---

## Context & guiding principle

Phase 1 produces a static snapshot refreshed manually. Phase 2 makes the model **live**: a netlink event source pushes add/remove/change events, they are debounced into stable domain events, and they flow through the **same** `DeviceService → EventBus → ViewModel → IUiDispatcher` path Phase 1 built.

**No new UI architecture.** This phase proves the existing event pipeline end-to-end against a real, asynchronous source. The only genuinely new machinery is (a) a netlink monitor, (b) a small reusable debounce/timer primitive, and (c) an incremental delta path on the existing service. If anything forces a redesign of the Phase 1 seams (`IUiDispatcher`, `EventBus`, the VMs), that is a design smell to fix — not to route around.

The PAL interface already exists from Phase 0 (`core/include/devmgr/pal/interfaces.hpp`):

```cpp
class IHotplugMonitor {
 public:
  using Callback = std::function<void(const HotplugEvent&)>;
  virtual core::Result<void> start(Callback callback) = 0;
  virtual void stop() = 0;
};
```

`HotplugEvent` (`core/include/devmgr/pal/hotplug_event.hpp`) already carries a **full mapped device**, not a thin action+syspath:

```cpp
struct HotplugEvent {
  enum class Action { Added, Removed, Changed };
  Action action;
  core::Device device;
};
```

This shape is load-bearing for the design: the **monitor itself maps** the netlink `udev_device` into a `core::Device`. There is no separate "re-read the device" step in the service, which also closes the race where a targeted re-read finds the device already gone.

---

## Components

### 1. `DelayedScheduler` — new runtime primitive (`devmgr_runtime`)

A small "run this after a delay, cancellable" timer. Needed because `TaskScheduler` is a plain `submit()` thread pool with **no delayed-execution capability**, and the debounce window requires one.

```cpp
class DelayedScheduler {
 public:
  using Handle = std::uint64_t;
  DelayedScheduler();                 // starts one timer thread
  ~DelayedScheduler();                // signals stop, drains/joins
  Handle schedule(std::chrono::milliseconds delay, std::function<void()> fn);
  void   cancel(Handle h);            // best-effort; no-op if already fired/unknown
};
```

- Implementation: one background thread; a min-heap of `(due_time, handle, fn)` keyed by `std::chrono::steady_clock`; `std::condition_variable::wait_until` on the earliest deadline. New/cancelled work notifies the cv to re-evaluate the wait.
- Clean shutdown mirrors the existing `TaskScheduler` destructor discipline: stop flag + `notify_all` + `join`.
- **Callbacks run on the timer thread** and must be cheap; heavy work is handed to `TaskScheduler`.
- Reused this phase by the status-line auto-clear and (later phases) update-progress timeouts — which is why it lives in `devmgr_runtime` rather than buried inside `HotplugService`.
- **Unit-tested directly:** fires after the delay; `cancel()` before fire suppresses; destruction with pending tasks does not deadlock or run cancelled work.

### 2. Shared udev mapping extraction (`devmgr_pal_linux`)

The enumerator's `mapDevice()` / `idFor()` / `prop()` / `attr()` / `s()` / `opt()` helpers currently live in an anonymous namespace inside `platform/linux/src/udev_device_enumerator.cpp`. The monitor needs the **identical** mapping so a hotplugged device produces a byte-for-byte equal `core::Device` (and therefore the same `stableId`) as a full enumeration — otherwise a plug followed by an `'r'` refresh would duplicate the device.

- Extract into an internal `platform/linux/src/udev_device_mapper.{hpp,cpp}`. This header **may** include `<libudev.h>` (it takes `udev_device*`); it is an internal platform translation unit, **not** a public `devmgr/` include, so the PAL public surface stays native-dep-free (the rule the enumerator header already follows).
- Lift the subsystem list `{pci, usb, platform, virtio}` into a shared `kSubsystems` constant so the enumerator and the monitor filter the **same** set.
- The enumerator is refactored to call the shared mapper (behavior-preserving; existing enumerator tests must stay green). This is the only Phase 1 refactor the work genuinely requires — no unrelated cleanup.

### 3. `UdevHotplugMonitor` (`devmgr_pal_linux`, implements `pal::IHotplugMonitor`)

Wraps a netlink `udev_monitor`.

- `start(Callback)`:
  - `udev_monitor_new_from_netlink(udev, "udev")`
  - per `kSubsystems`: `udev_monitor_filter_add_match_subsystem_devtype(mon, sub, nullptr)`
  - `udev_monitor_enable_receiving(mon)`; raise the socket receive buffer via `udev_monitor_set_receive_buffer_size` above the kernel default to survive event storms (exact size is a tuning constant, set at implementation)
  - grab `udev_monitor_get_fd(mon)`
  - spawn a dedicated reader thread that `poll()`s **two** fds: the monitor fd and a stop **eventfd**. On monitor-readable → `udev_monitor_receive_device` → read action (`udev_device_get_action`) → map action + device → invoke the callback. On stop-eventfd → break the loop.
  - returns `Result<void>` (error if monitor creation/enable fails).
- `stop()`: write the eventfd to wake the thread, `join`, free udev resources. **Idempotent** (safe to call twice / when never started).
- **Action mapping:** `add` → `Added`; `remove` → `Removed`; `change` / `bind` / `unbind` → `Changed`. Rationale: `bind`/`unbind` mean a driver attached/detached to an already-present device — a `boundDriver` change, not an add/remove.
- libudev resource ownership follows the enumerator's RAII deleter pattern (`udev_unref` / `udev_monitor_unref` / `udev_device_unref`); the `udev_device` from `receive_device` is **owned** (unref after mapping).
- **Exact libudev signatures verified via Context7 at plan kickoff** — the architecture above does not depend on the details.

### 4. `HotplugService` (`devmgr_app`) — debounce + drive the service

Owns the `IHotplugMonitor` lifecycle and performs **per-device trailing debounce**.

- State: `std::unordered_map<std::string /*DeviceId.value*/, PendingEntry>` where `PendingEntry { pal::HotplugEvent latest; DelayedScheduler::Handle flush; }`, guarded by a mutex (the monitor callback fires on the PAL reader thread).
- On each incoming event:
  - lock; overwrite `latest` for that device id; `cancel()` the prior flush handle; `schedule(window_, [id]{ flush(id); })`; unlock.
  - **Per-device** (not a single global timer) so one chatty device can't delay flushes for others — matching the outline's "debounce per-device".
- On `flush(id)` (timer thread): lock; move the entry out; unlock; then drive `DeviceService::applyDelta(latest)` (submitted to `TaskScheduler` so the timer thread stays free for the next deadline).
- **Window:** constructor parameter, default **250 ms**. No config system yet (YAGNI) — a typed default.
- Coalescing correctness falls out of the **idempotent** delta (§5): a never-seen `add→remove` collapses to a `Removed` that no-ops; repeated `change`s collapse to one `Changed` carrying the latest device.
- `start()` calls `monitor_.start(cb)`; `stop()` (and the destructor) calls `monitor_.stop()` and cancels all pending flush handles. Stopping the monitor first guarantees no callback races teardown.

### 5. `DeviceService::applyDelta()` (extended)

New incremental path beside `applyEnumeration`, reusing the **same** equality (`prev == dev`) and the same lock discipline (mutate under `mutex_`, **publish events outside** the lock):

```cpp
void applyDelta(const pal::HotplugEvent& ev);
```

- `Added` / `Changed`: absent → insert + publish `DeviceAddedEvent`; present & not equal → replace + publish `DeviceChangedEvent`; present & equal → **no-op**. (Robust to action mislabeling: a `Changed` for an unknown id becomes an add.)
- `Removed`: present → erase + publish `DeviceRemovedEvent`; absent → **no-op**.

Because both `applyDelta` and `applyEnumeration` take `mutex_`, a hotplug delta racing a manual `'r'` refresh is serialized and idempotent — the event/enumeration race is closed by construction. `'r'` manual refresh stays as a fallback.

### 6. TUI changes (no architectural change)

- **Selection-by-id** (`DeviceListVM`): `rebuild()` captures the current `selectedDeviceId()` **before** recomputing rows, then restores `selected_` to the row whose `DeviceId` matches; if the device vanished, clamp to the nearest valid row. This converts today's index-based selection (which shifts when devices appear/disappear) into id-stable selection.
- **Transient status line** — a small, toolkit-agnostic `StatusLineVM` (`devmgr_app`) subscribes to the three device events, formats the latest (e.g. `"usb device added: <name>"`), and schedules a clear via `DelayedScheduler` (~4 s), posting **both** the set and the clear through `IUiDispatcher`. The TUI renders `statusVM.text()` in a bottom status bar. To avoid flashing N messages during the initial bulk enumeration, the VM starts **disarmed** and ignores all events until `arm()` is called; `tui_app` calls `arm()` after the initial `facade.refresh()` future resolves and immediately before `startHotplug()`. Events fired by the initial `applyEnumeration` therefore produce no message; only genuine post-load hotplug (and subsequent manual-refresh) deltas do.

### 7. `ApplicationFacade` + wiring + lifecycle

- The facade gains `startHotplug()` / `stopHotplug()`, delegating to a referenced `HotplugService` — keeping the facade as the single frontend command/read surface, consistent with Phase 1's refs-only ownership style.
- `tui/src/tui_app.cpp` constructs `DelayedScheduler`, `UdevHotplugMonitor`, `HotplugService`, and `StatusLineVM`, **declared in an order whose reverse destruction tears the monitor down first** (stop reader thread → then `HotplugService` → then `DeviceService`/`EventBus`). `startHotplug()` on launch; `stopHotplug()` on exit **before** the FTXUI screen loop returns, so no event can post to a dead dispatcher.

---

## Data flow (end to end)

```
USB plug
  → kernel uevent → netlink
  → UdevHotplugMonitor reader thread: receive_device → map → HotplugEvent
  → HotplugService callback (PAL thread): coalesce into pending[id], (re)schedule flush
  → DelayedScheduler timer thread (after ~250ms quiet): flush(id)
  → TaskScheduler worker: DeviceService::applyDelta(ev)
  → EventBus::publish(DeviceAdded/Changed/Removed)  [synchronous]
  → DeviceListVM / StatusLineVM handlers: IUiDispatcher.post(rebuild / set-status)
  → UI thread: rebuild() (selection restored by id) + status bar renders
```

---

## Threading & lifecycle

- Three background threads touch this path: the PAL **reader thread** (monitor), the **timer thread** (`DelayedScheduler`), and `TaskScheduler` **workers**. The UI thread only renders.
- All VM-/UI-state mutation is marshaled through `IUiDispatcher` (same rule as Phase 1). The monitor callback, the debounce map, and `applyDelta` never touch VM state directly.
- Shutdown ordering is the critical correctness property: **monitor stops (reader thread joins) → HotplugService cancels pending flushes → DelayedScheduler/TaskScheduler/EventBus destruct**. Enforced by declaration order in `tui_app.cpp` plus an explicit `stopHotplug()` before the screen loop returns.

## Error handling & key risks

- **Clean shutdown / thread wakeup:** the reader thread must wake and join on `stop()` — done via a stop **eventfd** in the `poll()` set, never a blocking read with no wakeup.
- **Event/enumeration race:** closed by serializing `applyDelta` / `applyEnumeration` under one mutex and reconciling by `DeviceId` (idempotent).
- **Debounce window tuning:** too short → flicker; too long → sluggish. 250 ms default, single typed constant, adjustable.
- **Netlink buffer overflow under storms:** set an adequate receive buffer; per-device coalescing bounds downstream churn.
- **Monitor start failure** (no netlink, permissions): `start()` returns `Result<void>` error → surfaced as an `ErrorEvent`; the TUI still works in manual-`'r'` mode (graceful degradation).

---

## Testing & verification

- **Unit (FakePal-style, no native deps):** a `FakeHotplugMonitor` implementing `IHotplugMonitor` lets tests inject synthetic events, paired with a real `DelayedScheduler`. Assert:
  - a burst coalesces into one event;
  - `add→change→remove` produces the correct domain events in order;
  - `add→remove` within the window cancels cleanly (no phantom device);
  - `applyDelta` idempotency: add-existing-equal = no-op, remove-absent = no-op, change-unknown = add;
  - selection-by-id survives a list mutation;
  - `StatusLineVM` sets a message then auto-clears, and stays silent before first enumeration;
  - `HotplugService::stop()` joins without deadlock.
  - **`DelayedScheduler` direct tests:** fires after delay; `cancel` before fire; shutdown with pending tasks.
- **Integration (container, gated):** extend the umockdev rig — inject `add`/`remove` uevents (`umockdev_testbed_uevent`; exact API verified at kickoff) against a seeded device and assert the real `UdevHotplugMonitor` delivers a matching `HotplugEvent`.
- **Manual smoke (run by the user on real hardware):** launch `devmgr-tui`, plug/unplug a spare USB device, watch the list update live, selection stay put, and the status line appear/fade; cross-check with `udevadm monitor`.

---

## Out of scope

- Any device mutation (Phase 4).
- Qt GUI hotplug parity (Phase 3 — it reuses this exact pipeline unchanged).
- A configuration system for the debounce window (typed default only for now).

## API research — DONE (2026-06-30)

Signatures/semantics verified (high confidence) against systemd 260.1 man pages, the installed `libudev.h`, upstream `libudev-monitor.c`, and umockdev's own `umockdev.vala` / `test-umockdev.c`. Findings are folded into the addendum below and the implementation plan.

---

## Addendum — post-research refinements (2026-06-30)

These five points refine the approved body above after API verification. They strengthen correctness and shutdown determinism; the architecture is unchanged.

**R1 — Composition root owns hotplug; `ApplicationFacade` is unchanged.** Instead of adding `startHotplug()`/`stopHotplug()` to the facade (which would force a breaking constructor change rippling into existing facade tests), the **composition root** (`tui_app.cpp`, and later the Qt `main`) constructs and drives `HotplugService.start()/stop()` directly — exactly as it already owns `TaskScheduler`/`EventBus`/the enumerator and calls `facade.refresh()`. The ViewModels still receive updates purely via `EventBus`, so the facade stays a lean read/refresh surface. (Supersedes §7's "facade gains start/stop".)

**R2 — The debounce flush applies `applyDelta` directly on the `DelayedScheduler` timer thread**, *not* via `TaskScheduler`. `applyDelta` is cheap and non-blocking (map ops + synchronous `EventBus` publish; no I/O). Keeping it on the timer thread makes shutdown deterministic: stopping the `DelayedScheduler` halts *all* hotplug-driven model mutation, so there is no untracked work queued on `TaskScheduler` to drain at teardown. (Supersedes the "submitted to TaskScheduler" note in §4 and the corresponding box in the data-flow diagram.) Consequently `HotplugService` depends on `DelayedScheduler` + `DeviceService` only — **not** on `TaskScheduler`.

**R3 — `DelayedScheduler` exposes an idempotent `shutdown()`** (stop + join the timer thread; the destructor calls it). Teardown in the composition root is, in order: `hotplug.stop()` (signal eventfd → join the monitor reader thread → cancel pending flush handles) → `delayed.shutdown()` (join the timer thread so no flush/clear callback can still touch a ViewModel/`EventBus`) → wait on the in-flight `refresh()` futures (existing Phase 1 teardown) → stack unwinds. This guarantees no background thread publishes into a ViewModel whose subscription is being destroyed.

**R4 — The udev action→`HotplugEvent::Action` mapping is a pure, header-only function** `actionFromString(const char*) → std::optional<pal::HotplugEvent::Action>`, placed in the existing header-only `udev_field_mapping.hpp` so it is unit-tested with **no libudev dependency** (the test target already includes that header). Full mapping over the verified 8-string action set: `add`→`Added`; `remove`→`Removed`; `change`/`bind`/`unbind`/`move`/`online`/`offline`→`Changed`; `nullptr`/unknown→`std::nullopt` (event ignored). This means only the netlink *transport* needs the gated umockdev integration test; the action semantics are covered by fast unit tests.

**R5 — `UdevHotplugMonitor` re-validates the subsystem after receive.** The kernel BPF subsystem filter is a coarse pre-filter (and umockdev may not honor it), so after `udev_monitor_receive_device` the reader checks `udev_device_get_subsystem(dev)` against the shared `kSubsystems`; non-matching events are unref'd and skipped. The reader **drains** with `while ((dev = receive_device(mon)))` per `POLLIN` (the fd is non-blocking, so the loop ends at `NULL`/`EAGAIN`), handles `POLLERR`/`POLLHUP`/`POLLNVAL` on the monitor fd, and treats `poll()` `EINTR` as continue. Filters are installed **before** `udev_monitor_enable_receiving`. The monitor fd and `udev_device`s are owned by libudev and unref'd only **after** the reader thread is joined.
