# Phase 2 — Hotplug (live device updates) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the device list live — a netlink `udev_monitor` pushes add/remove/change events that are per-device debounced and flow through the existing `DeviceService → EventBus → ViewModel → IUiDispatcher` pipeline, so `devmgr-tui` updates without pressing `r`, keeps the selected device stable, and shows a transient status line.

**Architecture:** A new reusable `DelayedScheduler` timer primitive (`devmgr_runtime`) drives per-device debounce. `UdevHotplugMonitor` (`devmgr_pal_linux`) runs a netlink reader thread and maps each uevent to a `pal::HotplugEvent` reusing the enumerator's `mapDevice`. `HotplugService` (`devmgr_app`) coalesces events over a 250 ms window and calls a new idempotent `DeviceService::applyDelta`, which emits the same `DeviceAdded/Changed/Removed` events Phase 1 already renders. The composition root (`tui_app`) owns and starts/stops the monitor; two small ViewModel touches (selection-by-id, a `StatusLineVM`) complete the UX. No Phase 1 seam changes.

**Tech Stack:** C++20, libudev (`PkgConfig::UDEV`), FTXUI, GoogleTest, umockdev (gated integration), CMake + vcpkg.

## Global Constraints

- **Language/std:** C++20 (`target_compile_features(... cxx_std_20)`), matching existing targets.
- **Layering (do not violate):** `devmgr_app` links **only** `devmgr_core` — no ftxui, no libudev. `devmgr_pal_linux` public headers pull **NO** `<libudev.h>` (forward-declare opaque `struct udev`/`udev_monitor`; libudev is included only in `.cpp`). Header-only udev mapping helpers stay libudev-free.
- **Thread-safety:** all UI/ViewModel-state mutation is marshaled through `IUiDispatcher`. Model mutation (`DeviceService`) is under its mutex; events publish **outside** the lock.
- **Formatting/lint gates (must stay green):** `clang-format` (4-space indent, 100 col) and `clang-tidy --warnings-as-errors` over all sources — run per-file (batch invocation has a known path issue).
- **Commit cadence:** the **user commits each task** — the agent is denied `git add`/`git commit`. Each task ends with the exact commit command for the user to run at the review checkpoint; the executor stops there.
- **Gated verification (run by the user, not in-agent):** real-udev/umockdev integration tests (need `umockdev-wrapper`/Docker) and the TUI manual smoke run on the user's host. In-agent you may `cmake --build` and run the **unit** suite (`devmgr_tests`).
- **Reference spec:** `docs/superpowers/specs/2026-06-29-phase2-hotplug-design.md` (body + 2026-06-30 addendum R1–R5).

---

### Task 1: `DelayedScheduler` runtime timer primitive

**Files:**
- Create: `core/include/devmgr/runtime/delayed_scheduler.hpp`
- Create: `core/src/delayed_scheduler.cpp`
- Modify: `core/CMakeLists.txt` (add `src/delayed_scheduler.cpp` to `devmgr_core`)
- Test: `tests/unit/test_delayed_scheduler.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: nothing (leaf primitive).
- Produces: `devmgr::runtime::DelayedScheduler` with `using Handle = std::uint64_t;`, `Handle schedule(std::chrono::milliseconds delay, std::function<void()> fn)` (returns a non-zero handle), `void cancel(Handle handle)` (best-effort; safe on unknown/0), `void shutdown()` (idempotent stop+join; destructor calls it). Callbacks run on the single timer thread.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_delayed_scheduler.cpp`:

```cpp
#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/runtime/delayed_scheduler.hpp"

using devmgr::runtime::DelayedScheduler;
using namespace std::chrono_literals;

namespace {

// Busy-wait up to `timeout` for `pred` to become true; keeps the test fast when
// it passes and bounded when it regresses.
template <class Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

}  // namespace

TEST(DelayedScheduler, FiresCallbackAfterDelay) {
    DelayedScheduler timer;
    std::atomic<int> fired{0};
    timer.schedule(20ms, [&] { fired.fetch_add(1); });
    EXPECT_TRUE(waitFor([&] { return fired.load() == 1; }, 1s));
}

TEST(DelayedScheduler, CancelBeforeFireSuppressesCallback) {
    DelayedScheduler timer;
    std::atomic<int> fired{0};
    auto h = timer.schedule(100ms, [&] { fired.fetch_add(1); });
    timer.cancel(h);
    std::this_thread::sleep_for(150ms);
    EXPECT_EQ(fired.load(), 0);
}

TEST(DelayedScheduler, RunsDueCallbacksInOrderAcrossHandles) {
    DelayedScheduler timer;
    std::atomic<int> count{0};
    timer.schedule(10ms, [&] { count.fetch_add(1); });
    timer.schedule(20ms, [&] { count.fetch_add(1); });
    EXPECT_TRUE(waitFor([&] { return count.load() == 2; }, 1s));
}

TEST(DelayedScheduler, ShutdownWithPendingDoesNotRunCancelledAndJoins) {
    std::atomic<int> fired{0};
    {
        DelayedScheduler timer;
        timer.schedule(500ms, [&] { fired.fetch_add(1); });  // far in the future
        timer.shutdown();                                    // must not hang, must not fire
    }
    EXPECT_EQ(fired.load(), 0);
}
```

- [ ] **Step 2: Add the test to the build and run it to verify it fails**

Add to `tests/CMakeLists.txt` inside the `add_executable(devmgr_tests ...)` list (after `unit/test_task_scheduler.cpp`):

```cmake
    unit/test_delayed_scheduler.cpp
```

Run: `cmake --build build --target devmgr_tests`
Expected: FAIL to compile — `devmgr/runtime/delayed_scheduler.hpp` not found.

- [ ] **Step 3: Write the header**

Create `core/include/devmgr/runtime/delayed_scheduler.hpp`:

```cpp
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace devmgr::runtime {

// Runs callbacks after a delay on a single background thread. schedule() returns
// a Handle you may cancel() before it fires (best-effort). Callbacks run on the
// timer thread, so keep them cheap; a callback may itself schedule()/cancel().
// shutdown() (also run by the destructor) stops and joins the thread.
class DelayedScheduler {
   public:
    using Handle = std::uint64_t;

    DelayedScheduler();
    ~DelayedScheduler();
    DelayedScheduler(const DelayedScheduler&) = delete;
    DelayedScheduler& operator=(const DelayedScheduler&) = delete;

    Handle schedule(std::chrono::milliseconds delay, std::function<void()> fn);
    void cancel(Handle handle);
    void shutdown();

   private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        Handle id;
        std::function<void()> fn;
    };
    using Queue = std::multimap<Clock::time_point, Entry>;

    void run();

    std::mutex mutex_;
    std::condition_variable cv_;
    Queue queue_;
    std::unordered_map<Handle, Queue::iterator> index_;
    Handle nextId_ = 1;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace devmgr::runtime
```

- [ ] **Step 4: Write the implementation**

Create `core/src/delayed_scheduler.cpp`:

```cpp
#include "devmgr/runtime/delayed_scheduler.hpp"

#include <utility>
#include <vector>

namespace devmgr::runtime {

DelayedScheduler::DelayedScheduler() : worker_([this] { run(); }) {}

DelayedScheduler::~DelayedScheduler() {
    shutdown();
}

DelayedScheduler::Handle DelayedScheduler::schedule(std::chrono::milliseconds delay,
                                                    std::function<void()> fn) {
    const auto due = Clock::now() + delay;
    std::scoped_lock lock(mutex_);
    const Handle id = nextId_++;
    auto it = queue_.emplace(due, Entry{id, std::move(fn)});
    index_.emplace(id, it);
    cv_.notify_all();  // a possibly-earlier deadline arrived
    return id;
}

void DelayedScheduler::cancel(Handle handle) {
    std::scoped_lock lock(mutex_);
    auto found = index_.find(handle);
    if (found == index_.end()) return;
    queue_.erase(found->second);
    index_.erase(found);
    cv_.notify_all();
}

void DelayedScheduler::shutdown() {
    {
        std::scoped_lock lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void DelayedScheduler::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        if (stopping_) return;
        if (queue_.empty()) {
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            continue;
        }
        const auto due = queue_.begin()->first;
        if (Clock::now() < due) {
            cv_.wait_until(lock, due);
            continue;  // re-evaluate: earliest deadline or stop may have changed
        }
        // Collect everything due now, then run outside the lock.
        std::vector<std::function<void()>> ready;
        const auto now = Clock::now();
        while (!queue_.empty() && queue_.begin()->first <= now) {
            auto it = queue_.begin();
            index_.erase(it->second.id);
            ready.push_back(std::move(it->second.fn));
            queue_.erase(it);
        }
        lock.unlock();
        for (auto& fn : ready) fn();  // callbacks may schedule()/cancel() re-entrantly
        lock.lock();
    }
}

}  // namespace devmgr::runtime
```

- [ ] **Step 5: Add the source to the build**

Modify `core/CMakeLists.txt` — add to the `add_library(devmgr_core ...)` source list (after `src/task_scheduler.cpp`):

```cmake
    src/delayed_scheduler.cpp
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'DelayedScheduler' --output-on-failure`
Expected: PASS — 4 `DelayedScheduler.*` tests.

- [ ] **Step 7: Format + lint the new files**

Run: `clang-format -i core/include/devmgr/runtime/delayed_scheduler.hpp core/src/delayed_scheduler.cpp tests/unit/test_delayed_scheduler.cpp`
Run (per-file): `clang-tidy core/src/delayed_scheduler.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 8: Commit (user runs)**

```bash
git add core/include/devmgr/runtime/delayed_scheduler.hpp core/src/delayed_scheduler.cpp core/CMakeLists.txt tests/unit/test_delayed_scheduler.cpp tests/CMakeLists.txt
git commit -m "feat(runtime): DelayedScheduler — cancellable delayed callbacks on a timer thread"
```

---

### Task 2: `DeviceService::applyDelta` — idempotent incremental path

**Files:**
- Modify: `app/include/devmgr/app/device_service.hpp` (declare `applyDelta`, include hotplug_event)
- Modify: `app/src/device_service.cpp` (implement)
- Test: `tests/unit/test_device_service.cpp` (append cases)

**Interfaces:**
- Consumes: `pal::HotplugEvent` (`core/include/devmgr/pal/hotplug_event.hpp`), `core::Device::operator==` (defaulted), existing `runtime::EventBus`.
- Produces: `void DeviceService::applyDelta(const pal::HotplugEvent& event)` — Added/Changed insert-or-replace-or-noop; Removed erase-or-noop; publishes `DeviceAdded/Changed/RemovedEvent` outside the lock. Reused by `HotplugService` (Task 5).

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/test_device_service.cpp` (inside its anonymous namespace / test section; add `#include "devmgr/pal/hotplug_event.hpp"` at the top if absent):

```cpp
TEST(DeviceServiceDelta, AddThenNoopThenChangeThenRemove) {
    using devmgr::pal::HotplugEvent;
    devmgr::runtime::EventBus bus;
    devmgr::app::DeviceService service(bus);

    int added = 0, changed = 0, removed = 0;
    auto sA = bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto sC = bus.subscribe<devmgr::core::DeviceChangedEvent>([&](const auto&) { ++changed; });
    auto sR = bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { ++removed; });

    devmgr::core::Device dev;
    dev.id = devmgr::core::DeviceId{"dev-1"};
    dev.name = "Widget";

    service.applyDelta(HotplugEvent{HotplugEvent::Action::Added, dev});
    EXPECT_EQ(added, 1);
    EXPECT_EQ(service.devices().size(), 1u);

    // Added again, identical -> no-op (no event).
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Added, dev});
    EXPECT_EQ(added, 1);
    EXPECT_EQ(changed, 0);

    // Changed with a real difference -> DeviceChanged.
    dev.name = "Widget v2";
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Changed, dev});
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(service.findById(devmgr::core::DeviceId{"dev-1"})->name, "Widget v2");

    // Removed -> DeviceRemoved, model empty.
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Removed, dev});
    EXPECT_EQ(removed, 1);
    EXPECT_TRUE(service.devices().empty());
}

TEST(DeviceServiceDelta, ChangeOfUnknownActsAsAddAndRemoveOfAbsentIsNoop) {
    using devmgr::pal::HotplugEvent;
    devmgr::runtime::EventBus bus;
    devmgr::app::DeviceService service(bus);

    int added = 0, removed = 0;
    auto sA = bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto sR = bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { ++removed; });

    devmgr::core::Device dev;
    dev.id = devmgr::core::DeviceId{"ghost"};

    // Remove of absent device -> no-op.
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Removed, dev});
    EXPECT_EQ(removed, 0);

    // Change for an id we've never seen -> treated as Added.
    service.applyDelta(HotplugEvent{HotplugEvent::Action::Changed, dev});
    EXPECT_EQ(added, 1);
    EXPECT_EQ(service.devices().size(), 1u);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target devmgr_tests`
Expected: FAIL to compile — `DeviceService` has no member `applyDelta`.

- [ ] **Step 3: Declare `applyDelta`**

Modify `app/include/devmgr/app/device_service.hpp`: add the include and method.

Add near the top includes:
```cpp
#include "devmgr/pal/hotplug_event.hpp"
```
Add in the `public:` section after `applyEnumeration`:
```cpp
    void applyDelta(const pal::HotplugEvent& event);
```

- [ ] **Step 4: Implement `applyDelta`**

Modify `app/src/device_service.cpp`: add `#include <optional>` (top) and this method after `applyEnumeration`:

```cpp
void DeviceService::applyDelta(const pal::HotplugEvent& event) {
    std::optional<core::DeviceAddedEvent> added;
    std::optional<core::DeviceChangedEvent> changed;
    std::optional<core::DeviceRemovedEvent> removed;

    {
        std::scoped_lock lock(mutex_);
        const std::string key = event.device.id.value;
        auto it = model_.find(key);
        if (event.action == pal::HotplugEvent::Action::Removed) {
            if (it != model_.end()) {
                removed = core::DeviceRemovedEvent{it->second.id};
                model_.erase(it);
            }
        } else {  // Added or Changed — reconcile against the live model
            if (it == model_.end()) {
                model_.emplace(key, event.device);
                added = core::DeviceAddedEvent{event.device};
            } else if (!(it->second == event.device)) {
                it->second = event.device;
                changed = core::DeviceChangedEvent{event.device};
            }
        }
    }

    // Publish outside the lock (same discipline as applyEnumeration).
    if (removed) bus_.publish(*removed);
    if (added) bus_.publish(*added);
    if (changed) bus_.publish(*changed);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'DeviceServiceDelta' --output-on-failure`
Expected: PASS — both `DeviceServiceDelta.*` tests.

- [ ] **Step 6: Format + lint**

Run: `clang-format -i app/include/devmgr/app/device_service.hpp app/src/device_service.cpp tests/unit/test_device_service.cpp`
Run: `clang-tidy app/src/device_service.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 7: Commit (user runs)**

```bash
git add app/include/devmgr/app/device_service.hpp app/src/device_service.cpp tests/unit/test_device_service.cpp
git commit -m "feat(app): DeviceService::applyDelta — idempotent incremental hotplug path"
```

---

### Task 3: Shared udev mapping — `actionFromString` + extract `mapDevice`

**Files:**
- Modify: `platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp` (add `kSubsystems` + `actionFromString`)
- Create: `platform/linux/src/udev_device_mapper.hpp` (internal; may include `<libudev.h>`)
- Create: `platform/linux/src/udev_device_mapper.cpp` (moved `mapDevice`/`idFor` + helpers)
- Modify: `platform/linux/src/udev_device_enumerator.cpp` (call the shared mapper + `kSubsystems`)
- Modify: `platform/linux/CMakeLists.txt` (add `src/udev_device_mapper.cpp`)
- Test: `tests/unit/test_udev_field_mapping.cpp` (append `actionFromString` cases — header-only, no libudev)

**Interfaces:**
- Consumes: `pal::HotplugEvent::Action`, existing header-only helpers (`stableId`, `busFor`, `firstNonEmpty`, `strip0x`).
- Produces:
  - `inline constexpr std::array<std::string_view, 4> devmgr::platform_linux::kSubsystems{"pci","usb","platform","virtio"};`
  - `inline std::optional<pal::HotplugEvent::Action> devmgr::platform_linux::actionFromString(const char*)`.
  - `core::Device devmgr::platform_linux::mapDevice(udev_device*)` (from `udev_device_mapper.hpp`). Consumed by Task 4's monitor and the enumerator.

- [ ] **Step 1: Write the failing test (header-only action mapping)**

Append to `tests/unit/test_udev_field_mapping.cpp` (add `#include "devmgr/platform/linux/udev_field_mapping.hpp"` if not already present; it is included there today):

```cpp
TEST(UdevFieldMapping, ActionFromStringCoversTheFullUdevActionSet) {
    using devmgr::platform_linux::actionFromString;
    using Action = devmgr::pal::HotplugEvent::Action;

    EXPECT_EQ(actionFromString("add"), Action::Added);
    EXPECT_EQ(actionFromString("remove"), Action::Removed);
    for (const char* changed : {"change", "bind", "unbind", "move", "online", "offline"}) {
        EXPECT_EQ(actionFromString(changed), Action::Changed) << changed;
    }
    EXPECT_EQ(actionFromString(nullptr), std::nullopt);
    EXPECT_EQ(actionFromString("bogus"), std::nullopt);
}
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target devmgr_tests`
Expected: FAIL to compile — `actionFromString` undeclared.

- [ ] **Step 3: Add `kSubsystems` + `actionFromString` to the header-only mapping**

Modify `platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp`:

Add includes at the top (after existing includes):
```cpp
#include <array>
#include <optional>

#include "devmgr/pal/hotplug_event.hpp"
```
Add inside `namespace devmgr::platform_linux {` (e.g. just after the `fnv1a64` helper):
```cpp
// The subsystems this app models. Shared by the enumerator (scan filter) and the
// hotplug monitor (netlink match + post-receive re-validation) so they stay in sync.
inline constexpr std::array<std::string_view, 4> kSubsystems{"pci", "usb", "platform", "virtio"};

// Maps a udev action string (from udev_device_get_action) to a domain action.
// add->Added, remove->Removed, everything else that mutates an existing device
// (change/bind/unbind/move/online/offline)->Changed; unknown/null -> nullopt (ignore).
inline std::optional<pal::HotplugEvent::Action> actionFromString(const char* action) {
    if (action == nullptr) return std::nullopt;
    const std::string_view a(action);
    if (a == "add") return pal::HotplugEvent::Action::Added;
    if (a == "remove") return pal::HotplugEvent::Action::Removed;
    if (a == "change" || a == "bind" || a == "unbind" || a == "move" || a == "online" ||
        a == "offline") {
        return pal::HotplugEvent::Action::Changed;
    }
    return std::nullopt;
}
```

- [ ] **Step 4: Run the header-only test to verify it passes**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'UdevFieldMapping.ActionFromString' --output-on-failure`
Expected: PASS.

- [ ] **Step 5: Create the mapper header**

Create `platform/linux/src/udev_device_mapper.hpp`:

```cpp
#pragma once
#include <libudev.h>

#include "devmgr/core/models.hpp"

namespace devmgr::platform_linux {

// Maps a libudev device to our domain model. Shared by the enumerator and the
// hotplug monitor so a hotplugged device is byte-for-byte equal (same stableId)
// to the same device seen via a full enumeration. Internal header — includes
// <libudev.h> and is NOT part of the public devmgr/ surface.
core::Device mapDevice(udev_device* d);

}  // namespace devmgr::platform_linux
```

- [ ] **Step 6: Create the mapper implementation (moved verbatim from the enumerator)**

Create `platform/linux/src/udev_device_mapper.cpp` (the bare `"udev_device_mapper.hpp"` include resolves once Step 8 adds `src` as a PRIVATE include dir; nothing is built until Step 9):

```cpp
#include "udev_device_mapper.hpp"

#include <memory>
#include <optional>
#include <string>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

namespace devmgr::platform_linux {
namespace {

std::string s(const char* p) {
    return p != nullptr ? std::string(p) : std::string();
}
std::optional<std::string> opt(const char* p) {
    return p != nullptr ? std::optional<std::string>(p) : std::nullopt;
}
const char* prop(udev_device* d, const char* k) {
    return udev_device_get_property_value(d, k);
}
const char* attr(udev_device* d, const char* k) {
    return udev_device_get_sysattr_value(d, k);
}

std::string idFor(udev_device* d) {
    return stableId(
        s(udev_device_get_subsystem(d)), s(udev_device_get_syspath(d)),
        firstNonEmpty({prop(d, "ID_VENDOR_ID"), attr(d, "idVendor"), attr(d, "vendor")}),
        firstNonEmpty({prop(d, "ID_MODEL_ID"), attr(d, "idProduct"), attr(d, "device")}),
        firstNonEmpty({prop(d, "ID_SERIAL_SHORT"), prop(d, "ID_SERIAL")}));
}

}  // namespace

core::Device mapDevice(udev_device* d) {
    core::Device dev;
    dev.id = core::DeviceId{idFor(d)};
    dev.bus = busFor(s(udev_device_get_subsystem(d)));
    dev.sysfsPath = s(udev_device_get_syspath(d));
    dev.name = firstNonEmpty({prop(d, "ID_MODEL_FROM_DATABASE"), prop(d, "ID_MODEL"),
                              attr(d, "product"), udev_device_get_sysname(d)});
    dev.modalias = firstNonEmpty({prop(d, "MODALIAS"), attr(d, "modalias")});
    dev.vendorId =
        strip0x(firstNonEmpty({prop(d, "ID_VENDOR_ID"), attr(d, "idVendor"), attr(d, "vendor")}));
    dev.productId =
        strip0x(firstNonEmpty({prop(d, "ID_MODEL_ID"), attr(d, "idProduct"), attr(d, "device")}));
    dev.serial = firstNonEmpty({prop(d, "ID_SERIAL_SHORT"), prop(d, "ID_SERIAL")});
    dev.boundDriver = opt(udev_device_get_driver(d));
    dev.status = core::DeviceStatus::Active;

    if (udev_device* parent = udev_device_get_parent(d)) {  // BORROWED — no unref
        dev.parent = core::DeviceId{idFor(parent)};
    }

    udev_list_entry* p = nullptr;
    udev_list_entry_foreach(p, udev_device_get_properties_list_entry(d)) {
        dev.properties[s(udev_list_entry_get_name(p))] = s(udev_list_entry_get_value(p));
    }
    return dev;
}

}  // namespace devmgr::platform_linux
```

Note: `mapDevice`/`idFor`/`s`/`opt`/`prop`/`attr` are moved here **verbatim** from the enumerator's anonymous namespace (behavior-preserving); Step 7 deletes them from the enumerator. The `src` directory becomes a PRIVATE include dir in Step 8 so `"udev_device_mapper.hpp"` resolves from every `.cpp` in the target (enumerator, mapper, and the Task 4 monitor).

- [ ] **Step 7: Rewire the enumerator to use the shared mapper + `kSubsystems`**

Modify `platform/linux/src/udev_device_enumerator.cpp`:
1. Add include: `#include "udev_device_mapper.hpp"` and keep `#include "devmgr/platform/linux/udev_field_mapping.hpp"`.
2. **Delete** the moved helpers from its anonymous namespace: `s`, `opt`, `prop`, `attr`, `idFor`, and `mapDevice` (they now live in the mapper). **Keep** the RAII deleters (`UdevCtxDeleter`/`UdevEnumDeleter`/`UdevDevDeleter`) and `using` aliases.
3. Replace the hard-coded subsystem loop:
```cpp
    for (const char* sub : {"pci", "usb", "platform", "virtio"}) {
        udev_enumerate_add_match_subsystem(en.get(), sub);
    }
```
with:
```cpp
    for (std::string_view sub : kSubsystems) {
        udev_enumerate_add_match_subsystem(en.get(), std::string(sub).c_str());
    }
```
4. The `out.push_back(mapDevice(dev.get()));` call now resolves to the shared `mapDevice` (no code change at the call site).

- [ ] **Step 8: Add the mapper to the build**

Modify `platform/linux/CMakeLists.txt`:
```cmake
add_library(devmgr_pal_linux STATIC
    src/udev_device_enumerator.cpp
    src/udev_device_mapper.cpp)
target_include_directories(devmgr_pal_linux
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
```
Then set the mapper include in `udev_device_mapper.cpp` to `#include "udev_device_mapper.hpp"` (thanks to the new PRIVATE `src` include dir).

- [ ] **Step 9: Build; run unit tests; verify enumerator regression is green**

Run: `cmake --build build`
Run: `ctest --test-dir build -R 'UdevFieldMapping' --output-on-failure`
Expected: build OK; header-only mapping tests pass.
Note (gated — user/CI): the enumerator integration test (`devmgr_integration`) re-verifies `mapDevice` behavior under `umockdev-wrapper`; it must remain green, confirming the extraction is behavior-preserving.

- [ ] **Step 10: Format + lint**

Run: `clang-format -i platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp platform/linux/src/udev_device_mapper.hpp platform/linux/src/udev_device_mapper.cpp platform/linux/src/udev_device_enumerator.cpp tests/unit/test_udev_field_mapping.cpp`
Run: `clang-tidy platform/linux/src/udev_device_mapper.cpp platform/linux/src/udev_device_enumerator.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 11: Commit (user runs)**

```bash
git add platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp platform/linux/src/udev_device_mapper.hpp platform/linux/src/udev_device_mapper.cpp platform/linux/src/udev_device_enumerator.cpp platform/linux/CMakeLists.txt tests/unit/test_udev_field_mapping.cpp
git commit -m "refactor(pal-linux): extract shared udev mapDevice + kSubsystems + actionFromString"
```

---

### Task 4: `UdevHotplugMonitor` — netlink reader thread

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/udev_hotplug_monitor.hpp` (public — NO `<libudev.h>`)
- Create: `platform/linux/src/udev_hotplug_monitor.cpp`
- Modify: `platform/linux/CMakeLists.txt` (add source)
- Test: `tests/integration/test_udev_hotplug_monitor.cpp` (gated — umockdev)
- Modify: `tests/integration/CMakeLists.txt` (add source to `devmgr_integration_tests`)

**Interfaces:**
- Consumes: `pal::IHotplugMonitor` (Phase 0), `mapDevice` + `kSubsystems` + `actionFromString` (Task 3).
- Produces: `class devmgr::platform_linux::UdevHotplugMonitor final : public pal::IHotplugMonitor` with `core::Result<void> start(Callback)` / `void stop()`. Callback fires on the reader thread with a fully-mapped `pal::HotplugEvent`. Consumed by the composition root (Task 8).

- [ ] **Step 1: Write the failing integration test**

Create `tests/integration/test_udev_hotplug_monitor.cpp`:

```cpp
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>
#include <umockdev.h>

#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"

using namespace std::chrono_literals;

namespace {

// Thread-safe sink the monitor callback writes into (callback runs on the
// monitor's reader thread).
class EventSink {
   public:
    void push(const devmgr::pal::HotplugEvent& e) {
        std::scoped_lock lock(m_);
        events_.push_back(e);
        cv_.notify_all();
    }
    // Wait until at least `n` events are collected, or timeout.
    bool waitForCount(std::size_t n, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_);
        return cv_.wait_for(lock, timeout, [&] { return events_.size() >= n; });
    }
    std::vector<devmgr::pal::HotplugEvent> snapshot() {
        std::scoped_lock lock(m_);
        return events_;
    }

   private:
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<devmgr::pal::HotplugEvent> events_;
};

class UdevHotplugMonitorTest : public ::testing::Test {
   protected:
    UMockdevTestbed* bed_ = nullptr;
    void SetUp() override {
        bed_ = umockdev_testbed_new();
        ASSERT_NE(bed_, nullptr);
    }
    void TearDown() override {
        if (bed_ != nullptr) g_object_unref(bed_);
    }
};

TEST_F(UdevHotplugMonitorTest, DeliversAddThenRemoveForUsbDevice) {
    EventSink sink;
    devmgr::platform_linux::UdevHotplugMonitor monitor;
    auto started = monitor.start([&](const devmgr::pal::HotplugEvent& e) { sink.push(e); });
    ASSERT_TRUE(started.has_value()) << started.error().message;

    // add_device auto-emits an "add" uevent that our now-listening monitor sees.
    gchar* sys = umockdev_testbed_add_device(
        bed_, "usb", "1-1", nullptr, "idVendor", "1d6b", "idProduct", "0002", nullptr,
        "ID_VENDOR_ID", "1d6b", "ID_MODEL_ID", "0002", "SUBSYSTEM", "usb", nullptr);
    ASSERT_NE(sys, nullptr);

    ASSERT_TRUE(sink.waitForCount(1, 2s)) << "no add event delivered";
    auto afterAdd = sink.snapshot();
    EXPECT_EQ(afterAdd.front().action, devmgr::pal::HotplugEvent::Action::Added);
    EXPECT_EQ(afterAdd.front().device.bus, devmgr::core::BusType::Usb);
    const auto addedId = afterAdd.front().device.id;

    umockdev_testbed_uevent(bed_, sys, "remove");
    ASSERT_TRUE(sink.waitForCount(2, 2s)) << "no remove event delivered";
    auto afterRemove = sink.snapshot();
    EXPECT_EQ(afterRemove.back().action, devmgr::pal::HotplugEvent::Action::Removed);
    EXPECT_EQ(afterRemove.back().device.id, addedId);  // remove reconciles by the same id

    monitor.stop();  // must join cleanly
    g_free(sys);
}

}  // namespace
```

- [ ] **Step 2: Add the test to the integration build and confirm it fails**

Modify `tests/integration/CMakeLists.txt`:
```cmake
add_executable(devmgr_integration_tests test_udev_enumerator.cpp test_udev_hotplug_monitor.cpp)
```
Run: `cmake --build build --target devmgr_integration_tests`
Expected: FAIL to compile — `udev_hotplug_monitor.hpp` not found.

- [ ] **Step 3: Write the public header (libudev-free)**

Create `platform/linux/include/devmgr/platform/linux/udev_hotplug_monitor.hpp`:

```cpp
#pragma once
#include <atomic>
#include <thread>

#include "devmgr/pal/interfaces.hpp"

// Opaque libudev types — forward-declared so this public header stays free of
// <libudev.h> (same rule the enumerator header follows). Defined in the .cpp.
struct udev;
struct udev_monitor;

namespace devmgr::platform_linux {

// Real libudev netlink monitor. start() spawns a reader thread that translates
// uevents to pal::HotplugEvent and invokes the callback ON THAT THREAD. stop()
// wakes the thread via an eventfd, joins, then releases libudev resources.
class UdevHotplugMonitor final : public pal::IHotplugMonitor {
   public:
    UdevHotplugMonitor() = default;
    ~UdevHotplugMonitor() override;
    UdevHotplugMonitor(const UdevHotplugMonitor&) = delete;
    UdevHotplugMonitor& operator=(const UdevHotplugMonitor&) = delete;

    core::Result<void> start(Callback callback) override;
    void stop() override;

   private:
    void readLoop();
    void freeResources();  // unref monitor/udev, close eventfd (null/-1 guarded)

    Callback callback_;
    udev* udev_ = nullptr;
    udev_monitor* monitor_ = nullptr;
    int wakeFd_ = -1;
    std::thread reader_;
    std::atomic<bool> stopped_{true};  // true = not running
};

}  // namespace devmgr::platform_linux
```

- [ ] **Step 4: Write the implementation**

Create `platform/linux/src/udev_hotplug_monitor.cpp`:

```cpp
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"

#include <libudev.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <string>

#include "devmgr/platform/linux/udev_field_mapping.hpp"
#include "udev_device_mapper.hpp"

namespace devmgr::platform_linux {

UdevHotplugMonitor::~UdevHotplugMonitor() {
    stop();
}

core::Result<void> UdevHotplugMonitor::start(Callback callback) {
    if (!stopped_.load()) {
        return core::makeError(core::Error::Code::Io, "hotplug monitor already started");
    }
    callback_ = std::move(callback);

    udev_ = udev_new();
    if (udev_ == nullptr) return core::makeError(core::Error::Code::Io, "udev_new failed");

    monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
    if (monitor_ == nullptr) {
        freeResources();
        return core::makeError(core::Error::Code::Io, "udev_monitor_new_from_netlink failed");
    }

    for (std::string_view sub : kSubsystems) {
        udev_monitor_filter_add_match_subsystem_devtype(monitor_, std::string(sub).c_str(),
                                                        nullptr);
    }
    udev_monitor_set_receive_buffer_size(monitor_, 8 * 1024 * 1024);  // absorb enumeration bursts

    if (udev_monitor_enable_receiving(monitor_) < 0) {  // installs filters + binds socket
        freeResources();
        return core::makeError(core::Error::Code::Io, "udev_monitor_enable_receiving failed");
    }

    wakeFd_ = eventfd(0, EFD_CLOEXEC);
    if (wakeFd_ < 0) {
        freeResources();
        return core::makeError(core::Error::Code::Io, "eventfd failed");
    }

    stopped_.store(false);
    reader_ = std::thread([this] { readLoop(); });
    return {};
}

void UdevHotplugMonitor::stop() {
    if (stopped_.exchange(true)) return;  // idempotent; also guards double-join

    const std::uint64_t one = 1;
    [[maybe_unused]] const ssize_t w = ::write(wakeFd_, &one, sizeof one);  // wake poll()
    if (reader_.joinable()) reader_.join();  // reader no longer touches libudev objects
    freeResources();
}

void UdevHotplugMonitor::freeResources() {
    if (monitor_ != nullptr) monitor_ = udev_monitor_unref(monitor_);
    if (udev_ != nullptr) udev_ = udev_unref(udev_);
    if (wakeFd_ >= 0) {
        ::close(wakeFd_);
        wakeFd_ = -1;
    }
}

void UdevHotplugMonitor::readLoop() {
    struct pollfd fds[2];
    fds[0] = {udev_monitor_get_fd(monitor_), POLLIN, 0};
    fds[1] = {wakeFd_, POLLIN, 0};

    for (;;) {
        const int n = ::poll(fds, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;  // unexpected poll error
        }
        if (fds[1].revents & POLLIN) {  // stop requested
            std::uint64_t drain = 0;
            [[maybe_unused]] const ssize_t r = ::read(wakeFd_, &drain, sizeof drain);
            break;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;  // socket dead/overrun
        if ((fds[0].revents & POLLIN) == 0) continue;

        // Drain: the fd is non-blocking, so receive returns nullptr at end of queue.
        for (udev_device* dev; (dev = udev_monitor_receive_device(monitor_)) != nullptr;) {
            const char* subsystem = udev_device_get_subsystem(dev);
            const auto action = actionFromString(udev_device_get_action(dev));
            const bool modeled =
                subsystem != nullptr &&
                std::any_of(kSubsystems.begin(), kSubsystems.end(),
                            [&](std::string_view s) { return s == subsystem; });
            if (modeled && action.has_value()) {
                callback_(pal::HotplugEvent{*action, mapDevice(dev)});
            }
            udev_device_unref(dev);
        }
    }
}

}  // namespace devmgr::platform_linux
```

- [ ] **Step 5: Add the source to the pal build**

Modify `platform/linux/CMakeLists.txt` — add to the `add_library(devmgr_pal_linux STATIC ...)` list:
```cmake
    src/udev_hotplug_monitor.cpp
```

- [ ] **Step 6: Build (in-agent) and run the integration test (gated — user/CI)**

Run (in-agent — compiles + links libudev): `cmake --build build --target devmgr_integration_tests`
Expected: builds clean.
Gated (user/CI, under preload): `ctest --test-dir build -R 'devmgr_integration' --output-on-failure` — runs via `umockdev-wrapper` (LD_PRELOAD=libumockdev-preload.so). Expected: `UdevHotplugMonitorTest.DeliversAddThenRemoveForUsbDevice` PASS.

- [ ] **Step 7: Format + lint**

Run: `clang-format -i platform/linux/include/devmgr/platform/linux/udev_hotplug_monitor.hpp platform/linux/src/udev_hotplug_monitor.cpp tests/integration/test_udev_hotplug_monitor.cpp`
Run: `clang-tidy platform/linux/src/udev_hotplug_monitor.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 8: Commit (user runs)**

```bash
git add platform/linux/include/devmgr/platform/linux/udev_hotplug_monitor.hpp platform/linux/src/udev_hotplug_monitor.cpp platform/linux/CMakeLists.txt tests/integration/test_udev_hotplug_monitor.cpp tests/integration/CMakeLists.txt
git commit -m "feat(pal-linux): UdevHotplugMonitor — netlink reader thread with eventfd shutdown"
```

---

### Task 5: `HotplugService` — per-device debounce driving `applyDelta`

**Files:**
- Create: `app/include/devmgr/app/hotplug_service.hpp`
- Create: `app/src/hotplug_service.cpp`
- Modify: `app/CMakeLists.txt` (add source)
- Create: `tests/fakes/fake_hotplug_monitor.hpp`
- Test: `tests/unit/test_hotplug_service.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `pal::IHotplugMonitor` (Phase 0), `runtime::DelayedScheduler` (Task 1), `DeviceService::applyDelta` (Task 2).
- Produces: `class devmgr::app::HotplugService` with `HotplugService(pal::IHotplugMonitor&, DeviceService&, runtime::DelayedScheduler&, std::chrono::milliseconds window = 250ms)`, `core::Result<void> start()`, `void stop()`. Also `devmgr::test::FakeHotplugMonitor` (test double with `void emit(const pal::HotplugEvent&)`). Consumed by the composition root (Task 8).

- [ ] **Step 1: Write the fake monitor**

Create `tests/fakes/fake_hotplug_monitor.hpp`:

```cpp
#pragma once
#include <functional>
#include <utility>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// In-process IHotplugMonitor double. emit() synchronously invokes the callback
// on the caller's thread — deterministic for unit tests.
class FakeHotplugMonitor final : public pal::IHotplugMonitor {
   public:
    core::Result<void> start(Callback callback) override {
        callback_ = std::move(callback);
        started_ = true;
        return {};
    }
    void stop() override { started_ = false; }

    bool started() const { return started_; }
    void emit(const pal::HotplugEvent& event) {
        if (callback_) callback_(event);
    }

   private:
    Callback callback_;
    bool started_ = false;
};

}  // namespace devmgr::test
```

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/test_hotplug_service.cpp`:

```cpp
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/app/device_service.hpp"
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "fakes/fake_hotplug_monitor.hpp"

using namespace std::chrono_literals;

namespace {

devmgr::core::Device usbDevice(std::string id, std::string name) {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{std::move(id)};
    d.name = std::move(name);
    d.bus = devmgr::core::BusType::Usb;
    return d;
}

template <class Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

}  // namespace

TEST(HotplugService, CoalescesBurstIntoOneAddAfterWindow) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::app::DeviceService service(bus);
    devmgr::test::FakeHotplugMonitor monitor;

    std::atomic<int> added{0}, changed{0};
    auto sA = bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { added.fetch_add(1); });
    auto sC =
        bus.subscribe<devmgr::core::DeviceChangedEvent>([&](const auto&) { changed.fetch_add(1); });

    devmgr::app::HotplugService hotplug(monitor, service, timer, 30ms);
    ASSERT_TRUE(hotplug.start().has_value());
    ASSERT_TRUE(monitor.started());

    using Ev = devmgr::pal::HotplugEvent;
    monitor.emit(Ev{Ev::Action::Added, usbDevice("dev-1", "Widget")});
    monitor.emit(Ev{Ev::Action::Changed, usbDevice("dev-1", "Widget")});
    monitor.emit(Ev{Ev::Action::Changed, usbDevice("dev-1", "Widget v2")});

    EXPECT_TRUE(waitFor([&] { return added.load() == 1; }, 1s));
    EXPECT_EQ(changed.load(), 0);  // burst collapsed to a single Added carrying the latest device
    EXPECT_EQ(service.findById(devmgr::core::DeviceId{"dev-1"})->name, "Widget v2");

    hotplug.stop();
    EXPECT_FALSE(monitor.started());
}

TEST(HotplugService, AddThenRemoveWithinWindowCancelsCleanly) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::app::DeviceService service(bus);
    devmgr::test::FakeHotplugMonitor monitor;

    std::atomic<int> added{0}, removed{0};
    auto sA = bus.subscribe<devmgr::core::DeviceAddedEvent>([&](const auto&) { added.fetch_add(1); });
    auto sR =
        bus.subscribe<devmgr::core::DeviceRemovedEvent>([&](const auto&) { removed.fetch_add(1); });

    devmgr::app::HotplugService hotplug(monitor, service, timer, 30ms);
    ASSERT_TRUE(hotplug.start().has_value());

    using Ev = devmgr::pal::HotplugEvent;
    monitor.emit(Ev{Ev::Action::Added, usbDevice("dev-x", "Blip")});
    monitor.emit(Ev{Ev::Action::Removed, usbDevice("dev-x", "Blip")});

    // The net effect is a Remove of a never-committed device -> no-op, no phantom.
    std::this_thread::sleep_for(120ms);
    EXPECT_EQ(added.load(), 0);
    EXPECT_EQ(removed.load(), 0);
    EXPECT_TRUE(service.devices().empty());
    hotplug.stop();
}
```

- [ ] **Step 3: Add the test to the build; run to verify failure**

Add to `tests/CMakeLists.txt` `add_executable(devmgr_tests ...)` list:
```cmake
    unit/test_hotplug_service.cpp
```
Run: `cmake --build build --target devmgr_tests`
Expected: FAIL to compile — `devmgr/app/hotplug_service.hpp` not found.

- [ ] **Step 4: Write the header**

Create `app/include/devmgr/app/hotplug_service.hpp`:

```cpp
#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"

namespace devmgr::app {

// Owns the IHotplugMonitor lifecycle and per-device trailing debounce. Monitor
// events (on the reader thread) coalesce into a pending map; a DelayedScheduler
// flush (on the timer thread) applies the latest event per device to the
// DeviceService. applyDelta runs on the timer thread — no TaskScheduler hop —
// so stopping the timer halts all hotplug-driven model mutation (deterministic
// shutdown). Depends on DelayedScheduler + DeviceService only.
class HotplugService {
   public:
    HotplugService(pal::IHotplugMonitor& monitor, DeviceService& service,
                   runtime::DelayedScheduler& timer,
                   std::chrono::milliseconds window = std::chrono::milliseconds(250));
    ~HotplugService();
    HotplugService(const HotplugService&) = delete;
    HotplugService& operator=(const HotplugService&) = delete;

    core::Result<void> start();
    void stop();

   private:
    void onEvent(const pal::HotplugEvent& event);  // monitor reader thread
    void flush(const std::string& id);             // timer thread

    struct Pending {
        pal::HotplugEvent event;
        runtime::DelayedScheduler::Handle handle;
    };

    pal::IHotplugMonitor& monitor_;
    DeviceService& service_;
    runtime::DelayedScheduler& timer_;
    std::chrono::milliseconds window_;
    std::mutex mutex_;
    std::unordered_map<std::string, Pending> pending_;
};

}  // namespace devmgr::app
```

- [ ] **Step 5: Write the implementation**

Create `app/src/hotplug_service.cpp`:

```cpp
#include "devmgr/app/hotplug_service.hpp"

#include <optional>
#include <utility>

namespace devmgr::app {

HotplugService::HotplugService(pal::IHotplugMonitor& monitor, DeviceService& service,
                               runtime::DelayedScheduler& timer, std::chrono::milliseconds window)
    : monitor_(monitor), service_(service), timer_(timer), window_(window) {}

HotplugService::~HotplugService() {
    stop();
}

core::Result<void> HotplugService::start() {
    return monitor_.start([this](const pal::HotplugEvent& event) { onEvent(event); });
}

void HotplugService::stop() {
    monitor_.stop();  // joins the reader thread — onEvent will no longer be called
    std::scoped_lock lock(mutex_);
    for (auto& [id, pending] : pending_) timer_.cancel(pending.handle);
    pending_.clear();
}

void HotplugService::onEvent(const pal::HotplugEvent& event) {
    std::scoped_lock lock(mutex_);
    const std::string id = event.device.id.value;
    auto it = pending_.find(id);
    if (it != pending_.end()) {
        timer_.cancel(it->second.handle);  // reset this device's trailing window
        it->second.event = event;
    } else {
        it = pending_.emplace(id, Pending{event, 0}).first;
    }
    it->second.handle = timer_.schedule(window_, [this, id] { flush(id); });
}

void HotplugService::flush(const std::string& id) {
    std::optional<pal::HotplugEvent> event;
    {
        std::scoped_lock lock(mutex_);
        auto it = pending_.find(id);
        if (it == pending_.end()) return;  // cancelled/superseded
        event = it->second.event;
        pending_.erase(it);
    }
    service_.applyDelta(*event);  // cheap, non-blocking; publishes on this timer thread
}

}  // namespace devmgr::app
```

- [ ] **Step 6: Add the source to the build**

Modify `app/CMakeLists.txt` — add to `add_library(devmgr_app STATIC ...)`:
```cmake
    src/hotplug_service.cpp
```

- [ ] **Step 7: Run tests to verify they pass**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'HotplugService' --output-on-failure`
Expected: PASS — both `HotplugService.*` tests.

- [ ] **Step 8: Format + lint**

Run: `clang-format -i app/include/devmgr/app/hotplug_service.hpp app/src/hotplug_service.cpp tests/fakes/fake_hotplug_monitor.hpp tests/unit/test_hotplug_service.cpp`
Run: `clang-tidy app/src/hotplug_service.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 9: Commit (user runs)**

```bash
git add app/include/devmgr/app/hotplug_service.hpp app/src/hotplug_service.cpp app/CMakeLists.txt tests/fakes/fake_hotplug_monitor.hpp tests/unit/test_hotplug_service.cpp tests/CMakeLists.txt
git commit -m "feat(app): HotplugService — per-device debounce driving DeviceService::applyDelta"
```

---

### Task 6: `DeviceListVM` — preserve selection by `DeviceId`

**Files:**
- Modify: `app/src/device_list_vm.cpp` (`rebuild()` captures + restores selection by id)
- Test: `tests/unit/test_device_list_vm.cpp` (append case)

**Interfaces:**
- Consumes: existing `DeviceListVM` (`selectedDeviceId()`, `rowIds_`, `selected_`).
- Produces: no signature change — `rebuild()` now keeps the highlighted `DeviceId` stable across list mutations (falls back to nearest index if the device vanished).

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/test_device_list_vm.cpp` (reuse that file's existing harness for building a `DeviceListVM` over a `FakePal`-backed `ApplicationFacade` + `EventBus` + `InlineUiDispatcher`; mirror the existing tests' setup for seeding devices and triggering a rebuild). Add:

```cpp
TEST(DeviceListVM, PreservesSelectionByDeviceIdAcrossRebuild) {
    devmgr::test::FakePal pal;
    // Two USB devices; names chosen so inserting "Alpha" later shifts row order.
    pal.seedDevice([] {
        devmgr::core::Device d;
        d.id = devmgr::core::DeviceId{"dev-beta"};
        d.name = "Beta";
        d.bus = devmgr::core::BusType::Usb;
        return d;
    }());

    devmgr::runtime::EventBus bus;
    devmgr::runtime::TaskScheduler scheduler;
    devmgr::app::DeviceService service(bus);
    devmgr::app::ApplicationFacade facade(pal, scheduler, bus, service);
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::DeviceListVM vm(facade, bus, dispatcher);

    facade.refresh().wait();  // publishes Added; InlineUiDispatcher rebuilds synchronously

    // Select "Beta" by finding its row.
    auto rowOf = [&](const char* id) {
        for (int i = 0; i < static_cast<int>(vm.rowsRef().size()); ++i) {
            vm.selectedRef() = i;
            auto sel = vm.selectedDeviceId();
            if (sel && sel->value == id) return i;
        }
        return -1;
    };
    const int betaRow = rowOf("dev-beta");
    ASSERT_GE(betaRow, 0);
    vm.selectedRef() = betaRow;

    // A new device "Alpha" appears and sorts before "Beta", shifting indices.
    devmgr::core::Device alpha;
    alpha.id = devmgr::core::DeviceId{"dev-alpha"};
    alpha.name = "Alpha";
    alpha.bus = devmgr::core::BusType::Usb;
    service.applyDelta(devmgr::pal::HotplugEvent{devmgr::pal::HotplugEvent::Action::Added, alpha});

    // Selection must still resolve to Beta, not whatever now sits at the old index.
    auto sel = vm.selectedDeviceId();
    ASSERT_TRUE(sel.has_value());
    EXPECT_EQ(sel->value, "dev-beta");
}
```

(If `test_device_list_vm.cpp` lacks includes for `TaskScheduler`, `HotplugEvent`, or `applyDelta`, add: `#include "devmgr/pal/hotplug_event.hpp"` and `#include "devmgr/runtime/task_scheduler.hpp"`.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'DeviceListVM.PreservesSelectionByDeviceId' --output-on-failure`
Expected: FAIL — selection resolves to the shifted row (e.g. `dev-alpha` or a header), not `dev-beta`.

- [ ] **Step 3: Capture + restore selection in `rebuild()`**

Modify `app/src/device_list_vm.cpp`, `rebuild()`:

At the very top of `rebuild()` (before `rows_.clear();`), capture the currently selected id:
```cpp
    const std::optional<core::DeviceId> keep = selectedDeviceId();
```
Then replace the tail clamp block:
```cpp
    if (selected_ < 0) selected_ = 0;
    if (std::cmp_greater_equal(selected_, rows_.size()))
        selected_ = static_cast<int>(rows_.size()) - 1;
```
with:
```cpp
    if (keep) {  // keep the highlighted device stable across mutations
        for (std::size_t i = 0; i < rowIds_.size(); ++i) {
            if (rowIds_[i] == keep) {
                selected_ = static_cast<int>(i);
                break;
            }
        }
    }
    if (selected_ < 0) selected_ = 0;  // device vanished -> clamp to nearest valid row
    if (std::cmp_greater_equal(selected_, rows_.size()))
        selected_ = static_cast<int>(rows_.size()) - 1;
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'DeviceListVM' --output-on-failure`
Expected: PASS — the new case plus all existing `DeviceListVM.*` tests.

- [ ] **Step 5: Format + lint**

Run: `clang-format -i app/src/device_list_vm.cpp tests/unit/test_device_list_vm.cpp`
Run: `clang-tidy app/src/device_list_vm.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 6: Commit (user runs)**

```bash
git add app/src/device_list_vm.cpp tests/unit/test_device_list_vm.cpp
git commit -m "feat(app): DeviceListVM preserves selection by DeviceId across live rebuilds"
```

---

### Task 7: `StatusLineVM` — transient hotplug status

**Files:**
- Create: `app/include/devmgr/app/status_line_vm.hpp`
- Create: `app/src/status_line_vm.cpp`
- Modify: `app/CMakeLists.txt` (add source)
- Test: `tests/unit/test_status_line_vm.cpp`
- Modify: `tests/CMakeLists.txt` (add the test source)

**Interfaces:**
- Consumes: `runtime::EventBus`, `runtime::DelayedScheduler` (Task 1), `IUiDispatcher`, `core::Device*Event`.
- Produces: `class devmgr::app::StatusLineVM` with `StatusLineVM(runtime::EventBus&, runtime::DelayedScheduler&, IUiDispatcher&, std::chrono::milliseconds ttl = 4s)`, `void arm()`, `std::string text() const`. Starts disarmed (ignores events until `arm()`); shows the latest add/change/remove, auto-clears after `ttl`. Consumed by the composition root (Task 8).

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_status_line_vm.cpp`:

```cpp
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using namespace std::chrono_literals;

namespace {

devmgr::core::Device usb(std::string id, std::string name) {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{std::move(id)};
    d.name = std::move(name);
    d.bus = devmgr::core::BusType::Usb;
    return d;
}

template <class Pred>
bool waitFor(Pred pred, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

}  // namespace

TEST(StatusLineVM, StaysSilentUntilArmedThenShowsAndClears) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher, 40ms);

    // Disarmed: the initial enumeration's events produce no message.
    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-1", "Widget")});
    EXPECT_TRUE(vm.text().empty());

    vm.arm();
    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-2", "Gadget")});
    EXPECT_EQ(vm.text(), "usb device added: Gadget");

    // Auto-clears after the ttl.
    EXPECT_TRUE(waitFor([&] { return vm.text().empty(); }, 1s));
}

TEST(StatusLineVM, LatestEventWinsAndRemoveHasAGenericMessage) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher, 5s);  // long ttl; we assert the text
    vm.arm();

    bus.publish(devmgr::core::DeviceAddedEvent{usb("dev-2", "Gadget")});
    bus.publish(devmgr::core::DeviceChangedEvent{usb("dev-2", "Gadget v2")});
    EXPECT_EQ(vm.text(), "usb device changed: Gadget v2");

    bus.publish(devmgr::core::DeviceRemovedEvent{devmgr::core::DeviceId{"dev-2"}});
    EXPECT_EQ(vm.text(), "device removed");
}
```

- [ ] **Step 2: Add the test to the build; run to verify failure**

Add to `tests/CMakeLists.txt` `add_executable(devmgr_tests ...)`:
```cmake
    unit/test_status_line_vm.cpp
```
Run: `cmake --build build --target devmgr_tests`
Expected: FAIL to compile — `devmgr/app/status_line_vm.hpp` not found.

- [ ] **Step 3: Write the header**

Create `app/include/devmgr/app/status_line_vm.hpp`:

```cpp
#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>

#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Toolkit-agnostic transient status line. Subscribes to device deltas and shows
// the latest ("usb device added: <name>"), auto-clearing after `ttl` via a
// DelayedScheduler. Starts DISARMED and ignores events until arm() so the
// initial bulk enumeration produces no message. Handlers may run on the timer
// thread (applyDelta publishes there), so text_ is mutex-guarded; every update
// wakes the UI via IUiDispatcher::post.
class StatusLineVM {
   public:
    StatusLineVM(runtime::EventBus& bus, runtime::DelayedScheduler& timer,
                 IUiDispatcher& dispatcher,
                 std::chrono::milliseconds ttl = std::chrono::seconds(4));
    ~StatusLineVM();  // cancels any pending clear so no callback fires into a dead VM
    StatusLineVM(const StatusLineVM&) = delete;
    StatusLineVM& operator=(const StatusLineVM&) = delete;

    void arm() { armed_.store(true); }
    std::string text() const;

   private:
    void setMessage(std::string message);

    runtime::DelayedScheduler& timer_;
    IUiDispatcher& dispatcher_;
    std::chrono::milliseconds ttl_;
    std::atomic<bool> armed_{false};
    mutable std::mutex mutex_;
    std::string text_;
    runtime::DelayedScheduler::Handle clearHandle_ = 0;
    runtime::Subscription subAdded_;
    runtime::Subscription subRemoved_;
    runtime::Subscription subChanged_;
};

}  // namespace devmgr::app
```

- [ ] **Step 4: Write the implementation**

Create `app/src/status_line_vm.cpp`:

```cpp
#include "devmgr/app/status_line_vm.hpp"

#include <cctype>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string describe(const core::Device& d, const char* verb) {
    return lower(core::to_string(d.bus)) + " device " + verb + ": " + d.name;
}

}  // namespace

StatusLineVM::StatusLineVM(runtime::EventBus& bus, runtime::DelayedScheduler& timer,
                          IUiDispatcher& dispatcher, std::chrono::milliseconds ttl)
    : timer_(timer), dispatcher_(dispatcher), ttl_(ttl) {
    subAdded_ = bus.subscribe<core::DeviceAddedEvent>([this](const core::DeviceAddedEvent& e) {
        if (armed_.load()) setMessage(describe(e.device, "added"));
    });
    subChanged_ = bus.subscribe<core::DeviceChangedEvent>([this](const core::DeviceChangedEvent& e) {
        if (armed_.load()) setMessage(describe(e.device, "changed"));
    });
    subRemoved_ = bus.subscribe<core::DeviceRemovedEvent>([this](const core::DeviceRemovedEvent&) {
        // Removed carries only a DeviceId (the device is gone) — generic text.
        if (armed_.load()) setMessage("device removed");
    });
}

StatusLineVM::~StatusLineVM() {
    // Subscriptions unsubscribe as members destruct after this body. Cancel the
    // pending clear first so its callback can't run against a half-dead VM.
    std::scoped_lock lock(mutex_);
    if (clearHandle_ != 0) timer_.cancel(clearHandle_);
}

std::string StatusLineVM::text() const {
    std::scoped_lock lock(mutex_);
    return text_;
}

void StatusLineVM::setMessage(std::string message) {
    {
        std::scoped_lock lock(mutex_);
        text_ = std::move(message);
        if (clearHandle_ != 0) timer_.cancel(clearHandle_);
        clearHandle_ = timer_.schedule(ttl_, [this] {
            {
                std::scoped_lock inner(mutex_);
                text_.clear();
                clearHandle_ = 0;
            }
            dispatcher_.post([] {});  // wake the UI to re-render the cleared line
        });
    }
    dispatcher_.post([] {});  // wake the UI to re-render the new message
}

}  // namespace devmgr::app
```

- [ ] **Step 5: Add the source to the build**

Modify `app/CMakeLists.txt` — add to `add_library(devmgr_app STATIC ...)`:
```cmake
    src/status_line_vm.cpp
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `cmake --build build --target devmgr_tests && ctest --test-dir build -R 'StatusLineVM' --output-on-failure`
Expected: PASS — both `StatusLineVM.*` tests.

- [ ] **Step 7: Format + lint**

Run: `clang-format -i app/include/devmgr/app/status_line_vm.hpp app/src/status_line_vm.cpp tests/unit/test_status_line_vm.cpp`
Run: `clang-tidy app/src/status_line_vm.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 8: Commit (user runs)**

```bash
git add app/include/devmgr/app/status_line_vm.hpp app/src/status_line_vm.cpp app/CMakeLists.txt tests/unit/test_status_line_vm.cpp tests/CMakeLists.txt
git commit -m "feat(app): StatusLineVM — transient, armed-after-load hotplug status line"
```

---

### Task 8: Wire hotplug into `devmgr-tui`

**Files:**
- Modify: `tui/src/tui_app.cpp` (construct + start/stop monitor pipeline; render status bar; arm-after-initial)
- (No CMake change: `devmgr-tui` already links `devmgr_app`, `devmgr_core`, `devmgr_pal_linux`.)

**Interfaces:**
- Consumes: `DelayedScheduler` (Task 1), `UdevHotplugMonitor` (Task 4), `HotplugService` (Task 5), `DeviceListVM` selection-by-id (Task 6), `StatusLineVM` (Task 7), existing FTXUI wiring.
- Produces: a `devmgr-tui` that updates live, keeps selection stable, and shows the status line. Verified by manual smoke (gated).

- [ ] **Step 1: Add includes**

In `tui/src/tui_app.cpp`, add alongside the existing `devmgr/...` includes:
```cpp
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
```

- [ ] **Step 2: Construct the hotplug pipeline**

In `runTuiApp()`, after the existing construction block, add the timer, monitor, hotplug service, and status VM. The declaration order is chosen so that, combined with the explicit teardown in Step 5, no background thread outlives the ViewModels:

```cpp
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    runtime::DelayedScheduler delayed;
    platform_linux::UdevDeviceEnumerator enumerator;
    platform_linux::UdevHotplugMonitor monitor;
    app::DeviceService service(bus);
    app::ApplicationFacade facade(enumerator, scheduler, bus, service);
    app::HotplugService hotplug(monitor, service, delayed);  // 250 ms default window

    auto screen = ScreenInteractive::Fullscreen();
    FtxuiUiDispatcher dispatcher(screen);
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);
    app::StatusLineVM statusVm(bus, delayed, dispatcher);
```

- [ ] **Step 3: Render the status line**

Replace the outer `ui` renderer so the layout gains a bottom status bar. Change:
```cpp
    auto layout = Container::Horizontal({leftPane, detailRenderer});
    auto ui = Renderer(layout, [&] {
        return hbox({
                   vbox({
                       text(" Devices (r=refresh  q=quit) ") | bold,
                       separator(),
                       searchInput->Render(),
                       separator(),
                       deviceMenu->Render() | vscroll_indicator | yframe | flex,
                   }) | size(WIDTH, EQUAL, kLeftPaneWidth) |
                       border,
                   detailRenderer->Render() | border | flex,
               }) |
               flex;
    });
```
to:
```cpp
    auto layout = Container::Horizontal({leftPane, detailRenderer});
    auto ui = Renderer(layout, [&] {
        return vbox({
                   hbox({
                       vbox({
                           text(" Devices (r=refresh  q=quit) ") | bold,
                           separator(),
                           searchInput->Render(),
                           separator(),
                           deviceMenu->Render() | vscroll_indicator | yframe | flex,
                       }) | size(WIDTH, EQUAL, kLeftPaneWidth) |
                           border,
                       detailRenderer->Render() | border | flex,
                   }) | flex,
                   text(" " + statusVm.text() + " ") | inverted,
               }) |
               flex;
    });
```

- [ ] **Step 4: Populate synchronously, arm, then start hotplug — before the loop**

Replace the initial-populate line + loop:
```cpp
    pending.push_back(facade.refresh());  // initial populate without pressing 'r'
    screen.Loop(root);
```
with:
```cpp
    // Initial populate synchronously so the first frame is not empty and so the
    // status line stays silent for the initial enumeration (statusVm is armed
    // only afterward). Events published by refresh() are drained onto this (UI)
    // thread before the loop starts.
    facade.refresh().wait();
    dispatcher.drain();
    statusVm.arm();
    if (auto started = hotplug.start(); !started) {
        // Degrade gracefully: without live events, 'r' refresh still works.
        bus.publish(core::ErrorEvent{.source = "hotplug", .message = started.error().message});
    }

    screen.Loop(root);
```

- [ ] **Step 5: Deterministic teardown before the ViewModels unwind**

Immediately after `screen.Loop(root);` returns, and **before** the existing `pending` wait loop, add:
```cpp
    // Stop event sources before the VMs/dispatcher unwind: join the monitor
    // reader thread (no new events into the debounce map), then join the timer
    // thread (no flush/clear callback can still publish into a VM being torn
    // down). Order: monitor -> timer -> drain in-flight refreshes.
    hotplug.stop();
    delayed.shutdown();
```
The existing `for (auto& f : pending) { if (f.valid()) f.wait(); }` block stays as-is after these two lines.

- [ ] **Step 6: Build and run the unit suite (in-agent)**

Run: `cmake --build build`
Run: `ctest --test-dir build --output-on-failure`
Expected: full build clean; all unit tests pass (the wiring change is compile-checked; behavior is smoke-verified next).

- [ ] **Step 7: Manual smoke (gated — user, real host)**

Run: `./build/tui/devmgr-tui`
Verify:
1. The list populates on first paint (no `r` needed).
2. Plug a spare USB device → within ~250 ms it appears; status bar shows `usb device added: <name>` then clears after ~4 s.
3. With a device selected, plug/unplug another → the selection stays on the same device (does not jump).
4. Unplug the device → it disappears; status bar shows `device removed`.
5. Cross-check the stream with `udevadm monitor` in another terminal.
6. Press `q` → clean exit, no hang (reader + timer threads joined).

- [ ] **Step 8: Format + lint**

Run: `clang-format -i tui/src/tui_app.cpp`
Run: `clang-tidy tui/src/tui_app.cpp -p build --warnings-as-errors='*'`
Expected: no diffs, no warnings.

- [ ] **Step 9: Commit (user runs)**

```bash
git add tui/src/tui_app.cpp
git commit -m "feat(tui): wire live hotplug — monitor + debounce + status line into devmgr-tui"
```

---

## Whole-branch verification (after Task 8)

- Build clean: `cmake --build build`
- Unit suite green: `ctest --test-dir build --output-on-failure` (all `devmgr_tests`)
- Gated (user/CI): `ctest` under `umockdev-wrapper` for `devmgr_integration` (enumerator + hotplug monitor); TUI manual smoke per Task 8 Step 7.
- Format/lint gate over all changed files (per-file `clang-format` + `clang-tidy --warnings-as-errors`).
- Update roadmap memory: mark Phase 2 status; note branch state.
