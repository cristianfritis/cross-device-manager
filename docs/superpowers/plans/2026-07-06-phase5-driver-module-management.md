# Phase 5 — Driver/Module Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Universal persistent enable/disable (USB `authorized` + driver unbind for every other bus) with a daemon-side StateStore and active enforcement, privileged module load/unload with a full error taxonomy, the libkmod read side (driver info, module list, signatures, modprobe.d read-only), surgical bind/unbind verbs, and a Modules view + device-detail Driver section in both UIs.

**Architecture:** devmgrd gains `pal::IDriverManager&` + `StateStore&` + an `EnforcementService` (startup sweep + hotplug watch); libkmod is quarantined in `platform/linux::KmodDriverManager` which serves frontends (reads) and the daemon (writes). IPC bumps to `ApiVersion=2` with `LoadModule`/`UnloadModule`/`BindDriver`/`UnbindDriver`/`ListDisabledDevices`. Frontends merge the daemon's desired-state (one bulk call per refresh) so disabled devices render Disabled even during the kernel's replug-rebind flicker.

**Tech Stack:** C++20, libkmod (pkg-config), nlohmann-json (vcpkg, already in manifest), sdbus-c++ v2 (existing), FTXUI, Qt6, GTest.

**Spec:** `docs/superpowers/specs/2026-07-06-phase5-driver-module-management-design.md` — normative for every behavior below.

## Global Constraints

- Branch: `feature/phase5` (stacked on `feature/phase4`). **The agent MUST NOT run `git add`/`git commit` — the USER commits every task.** Commit steps below print the exact command for the user.
- Gates per task: build (`cmake --build --preset linux-debug`), `ctest --test-dir build/linux-debug --output-on-failure`, `clang-format --dry-run --Werror` on touched files, clang-tidy clean on touched `core/app/platform/daemon/gui` sources.
- sdbus-c++ **v2 API only**; sdbus includes allowed ONLY in `daemon/src/*` and `platform/linux/src/dbus_privileged_channel.cpp` (CI purity guard greps for violations). `dbus_contract.hpp` stays sdbus-free.
- No Qt includes in `core/app/platform/daemon` (CI toolkit purity guard).
- libkmod linked PRIVATE into `devmgr_pal_linux` only; `<libkmod.h>` appears ONLY in `platform/linux/src/*` (same quarantine style as libudev; `kmod_driver_manager.hpp` must not include it).
- The privileged daemon never fork/execs. modprobe.d `install`-command modules are refused, not executed.
- Pipeline order for every daemon verb: **validate → guard → authorize → act** (a refused request must never trigger a polkit prompt).
- `SetDeviceEnabled` = persistent (StateStore); `BindDriver`/`UnbindDriver` = surgical, NEVER touch the StateStore.
- Docker already installs `libkmod-dev`; `vcpkg.json` already lists `nlohmann-json`. CI file lists are glob-based (`find core tests app platform tui gui daemon`, `daemon/src/*.cpp` etc.) — new files are picked up automatically; do not edit `.github/workflows/ci.yml` unless a step fails.
- The tree must still configure/build with no sdbus-c++, no Qt, and (new) no libkmod is NOT required — libkmod is REQUIRED on Linux (`pkg_check_modules(KMOD REQUIRED IMPORTED_TARGET libkmod)`): it is universally present (it ships `modprobe`).

## File Map (created ▸ / modified ●)

```
● core/include/devmgr/core/models.hpp            Driver.signer, LoadedModule, ModprobeInfo, DeviceKey, DisabledDeviceEntry
● core/include/devmgr/core/events.hpp            + ModulesChangedEvent
▸ core/include/devmgr/services/device_key.hpp    key build/match (pure)
▸ core/src/device_key.cpp
● core/include/devmgr/services/critical_device_guard.hpp   + ModuleUnloadFacts, evaluateModuleUnload
● core/src/critical_device_guard.cpp
● core/include/devmgr/pal/interfaces.hpp         IDeviceController v2, IDriverManager v2, IPrivilegedChannel v2, ISystemInfo.lockdownMode
▸ platform/linux/include/devmgr/platform/linux/kmod_driver_manager.hpp
▸ platform/linux/src/kmod_driver_manager.cpp     libkmod RW + sysfs walks (quarantine)
▸ platform/linux/include/devmgr/platform/linux/linux_system_info.hpp
▸ platform/linux/src/linux_system_info.cpp
● platform/linux/src/sysfs_device_controller.cpp v2: unbind/bind/driver_override
● platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp
● platform/linux/src/udev_device_mapper.cpp      (no change needed — overlay happens app-side)
● platform/linux/include/devmgr/platform/linux/dbus_contract.hpp   ApiVersion=2
● platform/linux/src/dbus_privileged_channel.cpp v2 verbs + ApiVersion cache
● platform/linux/include/devmgr/platform/linux/dbus_privileged_channel.hpp
▸ daemon/include/devmgr/daemon/state_store.hpp
▸ daemon/src/state_store.cpp
▸ daemon/include/devmgr/daemon/enforcement_service.hpp
▸ daemon/src/enforcement_service.cpp
● daemon/include/devmgr/daemon/request_processor.hpp   v2 ctor + 5 new verbs
● daemon/src/request_processor.cpp
● daemon/src/manager_adaptor.cpp                 5 new methods
● daemon/src/main.cpp                            --state-dir, kmod, enforcement wiring
● daemon/data/org.devmgr.policy                  + manage-modules, manage-drivers
● daemon/CMakeLists.txt                          + state_store, enforcement, nlohmann_json
● app/include/devmgr/app/application_facade.hpp  v2 reads/mutations/advisories
● app/src/application_facade.cpp
▸ app/include/devmgr/app/disabled_overlay.hpp    pure overlay fn
▸ app/src/disabled_overlay.cpp
▸ app/include/devmgr/app/modules_vm.hpp
▸ app/src/modules_vm.cpp
● app/src/device_detail_vm.cpp                   driver section
● app/CMakeLists.txt
● tui/src/tui_app.cpp                            Modules screen, m/l/u/U/B keys
▸ gui/src/module_list_model.hpp / .cpp
● gui/src/main_window.hpp / .cpp                 tabs, module actions, bind/unbind
● gui/src/gui_app.cpp                            wiring
● gui/CMakeLists.txt
● tests/fakes/fake_pal.hpp                       IDriverManager v2 + scripting
● tests/fakes/fake_privileged_channel.hpp        v2 verbs
▸ tests/unit/test_device_key.cpp
▸ tests/unit/test_state_store.cpp
▸ tests/unit/test_enforcement_service.cpp
▸ tests/unit/test_kmod_driver_manager.cpp        fixture-driven (no real .ko)
▸ tests/unit/test_linux_system_info.cpp
▸ tests/unit/test_disabled_overlay.cpp
▸ tests/unit/test_modules_vm.cpp
● tests/unit/test_critical_device_guard.cpp      + evaluateModuleUnload matrix
● tests/unit/test_sysfs_device_controller.cpp    + unbind/override cases
● tests/unit/test_request_processor.cpp          + new verbs
● tests/unit/test_application_facade.cpp         + new methods
● tests/ipc/test_ipc_round_trip.cpp              + v2 verbs, persistence restart
● tests/CMakeLists.txt / daemon deps
▸ gui/tests/test_module_list_model.cpp
● gui/tests/test_main_window.cpp
▸ test/vm/phase5-smoke.sh  ● test-vm.sh  ● README.md
```

Task order: T1 types/interfaces → T2 guard → T3 controller → T4 store → T5 kmod read → T6 kmod write + sysinfo → T7 processor → T8 enforcement → T9 IPC → T10 facade/VMs → T11 TUI → T12 GUI → T13 VM/docs/final gates.

---

### Task 1: Core models, DeviceKey, and PAL interface v2 (tree stays green)

**Files:**
- Modify: `core/include/devmgr/core/models.hpp`
- Modify: `core/include/devmgr/core/events.hpp`
- Create: `core/include/devmgr/services/device_key.hpp`, `core/src/device_key.cpp`
- Modify: `core/include/devmgr/pal/interfaces.hpp`
- Modify: `core/CMakeLists.txt` (add `src/device_key.cpp`)
- Modify (mechanical, keep compiling): `platform/linux/src/sysfs_device_controller.cpp`, `platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp`, `daemon/src/request_processor.cpp` (call-site arg only), `tests/fakes/fake_pal.hpp`, `tests/fakes/fake_privileged_channel.hpp`, `tests/unit/test_sysfs_device_controller.cpp`, `tests/unit/test_application_facade.cpp` (FakePal seeding signature)
- Test: `tests/unit/test_device_key.cpp` (+ register in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: existing `core::Device`, `core::Driver`, `Result`.
- Produces (later tasks rely on these EXACT shapes):

```cpp
// models.hpp additions (inside namespace devmgr::core)
struct LoadedModule {
    std::string name;
    std::uint64_t sizeBytes = 0;
    long refCount = 0;
    std::vector<std::string> holders;
    friend bool operator==(const LoadedModule&, const LoadedModule&) = default;
};

struct ModprobeInfo {
    std::optional<std::string> options;  // concatenated modprobe.d options, nullopt if none
    bool blacklisted = false;
    friend bool operator==(const ModprobeInfo&, const ModprobeInfo&) = default;
};

// Tiered identity (spec §5.1): serial tuple when serial != "", else position
// validated by vendor/product. Field values are lowercase hex ids WITHOUT 0x
// (mapper convention) and the bus string from services::keyBusString().
struct DeviceKey {
    std::string bus;        // "usb" | "pci" | "platform" | "virtio" | "other"
    std::string vendorId;
    std::string productId;
    std::string serial;     // "" => positional matching
    std::string position;   // usb port chain "2-1.4" | pci address "0000:03:00.0"
    friend bool operator==(const DeviceKey&, const DeviceKey&) = default;
};

struct DisabledDeviceEntry {
    DeviceKey key;
    std::string mechanism;      // "authorized" | "unbind"
    std::string lastDriver;     // "" when unknown (plain drivers_probe rebind)
    std::string lastSysfsPath;  // display/debug + fallback match
    std::int64_t disabledAtUtc = 0;
    bool guardSuspended = false;
    friend bool operator==(const DisabledDeviceEntry&, const DisabledDeviceEntry&) = default;
};
```

`Driver` gains one field after `isSigned`: `std::optional<std::string> signer;`
`events.hpp` gains: `struct ModulesChangedEvent {};`

```cpp
// device_key.hpp (namespace devmgr::services)
#pragma once
#include <string>
#include <vector>
#include "devmgr/core/models.hpp"

namespace devmgr::services {

// Stable serialization bus string (independent of core::to_string casing).
std::string keyBusString(core::BusType bus);

// USB: last path segment when it looks like a port chain ("2-1.4").
// Everything else: last path segment verbatim ("0000:03:00.0").
std::string positionFor(core::BusType bus, const std::string& sysfsPath);

// Plain key from one device.
core::DeviceKey makeDeviceKey(const core::Device& device);

// Cloned-serial guard (spec §5.1): if any OTHER present device shares the
// (bus, vendorId, productId, serial) tuple, the key is downgraded to
// positional (serial forced to "").
core::DeviceKey makeDeviceKey(const core::Device& device,
                              const std::vector<core::Device>& present);

// Tier 1: serial tuple. Tier 2: bus+position, validated by vendor/product —
// a vendor/product mismatch at the stored position returns false.
bool matchesDevice(const core::DeviceKey& key, const core::Device& device);

}  // namespace devmgr::services
```

- `pal/interfaces.hpp` v2 (COMPLETE new bodies of the three changed interfaces):

```cpp
class IDeviceController {
   public:
    virtual ~IDeviceController() = default;
    // Identity is the device's canonical sysfs path. Phase 5 mechanisms:
    //  - `authorized` attr present (USB): write 0/1. Returns nullopt.
    //  - otherwise: disable = unbind current driver (returns its name, ""
    //    if none was bound — the value signals mechanism "unbind");
    //    enable = write rebindDriverHint to driver_override (when non-empty
    //    and the attr exists), then bus drivers_probe; override cleared even
    //    on failure. Returns nullopt on enables and authorized-mechanism ops.
    virtual core::Result<std::optional<std::string>> setEnabled(
        const std::string& sysfsPath, bool enabled, const std::string& rebindDriverHint) = 0;
    // Surgical verbs (never persisted by callers).
    virtual core::Result<void> bindDriver(const std::string& sysfsPath,
                                          const std::string& driverName) = 0;
    virtual core::Result<void> unbindDriver(const std::string& sysfsPath) = 0;
};

class IDriverManager {
   public:
    virtual ~IDriverManager() = default;
    // Takes the full Device: modalias lookup + driver-symlink resolution need
    // modalias and sysfsPath (spec §4.2 refinement; same rationale as
    // IPrivilegedChannel taking Device). Returns the modalias candidate list;
    // the bound one is identified by the caller via Device::boundDriver.
    virtual core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
    virtual core::Result<std::vector<core::LoadedModule>> listLoadedModules() = 0;
    virtual core::Result<core::Driver> moduleInfo(const std::string& name) = 0;
    virtual core::Result<core::ModprobeInfo> modprobeInfo(const std::string& name) = 0;
    // Canonical sysfs device paths bound via any of the module's drivers.
    virtual core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) = 0;
};

class IPrivilegedChannel {
   public:
    virtual ~IPrivilegedChannel() = default;
    virtual core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
    virtual core::Result<void> bindDriver(const core::Device& device,
                                          const std::string& driverName) = 0;
    virtual core::Result<void> unbindDriver(const core::Device& device) = 0;
    virtual core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() = 0;
};
```

`ISystemInfo::Info` gains after `rebootPending`: `std::string lockdownMode = "none";`

- [x] **Step 1: Write the failing tests**

Create `tests/unit/test_device_key.cpp`:

```cpp
#include <gtest/gtest.h>

#include "devmgr/services/device_key.hpp"

using devmgr::core::BusType;
using devmgr::core::Device;
using devmgr::core::DeviceKey;
namespace svc = devmgr::services;

namespace {
Device usb(std::string path, std::string vid, std::string pid, std::string serial) {
    Device d;
    d.bus = BusType::Usb;
    d.sysfsPath = std::move(path);
    d.vendorId = std::move(vid);
    d.productId = std::move(pid);
    d.serial = std::move(serial);
    return d;
}
}  // namespace

TEST(DeviceKey, UsbPositionIsThePortChainSegment) {
    EXPECT_EQ(svc::positionFor(BusType::Usb, "/sys/devices/pci0000:00/usb2/2-1/2-1.4"), "2-1.4");
}

TEST(DeviceKey, PciPositionIsTheAddressSegment) {
    EXPECT_EQ(svc::positionFor(BusType::Pci, "/sys/devices/pci0000:00/0000:03:00.0"),
              "0000:03:00.0");
}

TEST(DeviceKey, SerialTupleMatchesAcrossPorts) {
    const auto key = svc::makeDeviceKey(usb("/sys/devices/pci0000:00/usb2/2-1", "046d", "c52b", "AB12"));
    // Same physical device replugged at a different port: still matches.
    EXPECT_TRUE(svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb1/1-3", "046d", "c52b", "AB12")));
    // Different serial: no match.
    EXPECT_FALSE(svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb1/1-3", "046d", "c52b", "XX")));
}

TEST(DeviceKey, SerialLessMatchesByPositionValidatedByIds) {
    const auto key = svc::makeDeviceKey(usb("/sys/devices/pci0000:00/usb2/2-1.4", "1a2b", "3c4d", ""));
    EXPECT_EQ(key.serial, "");
    EXPECT_EQ(key.position, "2-1.4");
    EXPECT_TRUE(svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb2/2-1.4", "1a2b", "3c4d", "")));
    // Same port, DIFFERENT device: validation refuses (never enforce on a stranger).
    EXPECT_FALSE(svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb2/2-1.4", "dead", "beef", "")));
    // Serial-less device moved port = new device (Windows semantics).
    EXPECT_FALSE(svc::matchesDevice(key, usb("/sys/devices/pci0000:00/usb1/1-2", "1a2b", "3c4d", "")));
}

TEST(DeviceKey, ClonedSerialDowngradesToPositional) {
    const auto target = usb("/sys/devices/pci0000:00/usb2/2-1", "aaaa", "bbbb", "0123456789");
    const auto clone = usb("/sys/devices/pci0000:00/usb2/2-2", "aaaa", "bbbb", "0123456789");
    const auto key = svc::makeDeviceKey(target, {target, clone});
    EXPECT_EQ(key.serial, "");        // downgraded
    EXPECT_EQ(key.position, "2-1");   // pinned to the port instead
    // Unique serial is NOT downgraded.
    const auto unique = usb("/sys/devices/pci0000:00/usb2/2-1", "aaaa", "bbbb", "UNIQ");
    EXPECT_EQ(svc::makeDeviceKey(unique, {unique, clone}).serial, "UNIQ");
}

TEST(DeviceKey, BusStringsAreStable) {
    EXPECT_EQ(svc::keyBusString(BusType::Usb), "usb");
    EXPECT_EQ(svc::keyBusString(BusType::Pci), "pci");
    EXPECT_EQ(svc::keyBusString(BusType::Platform), "platform");
    EXPECT_EQ(svc::keyBusString(BusType::Virtio), "virtio");
    EXPECT_EQ(svc::keyBusString(BusType::Other), "other");
}
```

Register in `tests/CMakeLists.txt` (add `unit/test_device_key.cpp` to the `add_executable(devmgr_tests ...)` list, after `test_critical_device_guard.cpp`).

- [x] **Step 2: Run to verify failure**

Run: `cmake --build --preset linux-debug 2>&1 | tail -5`
Expected: FAIL — `devmgr/services/device_key.hpp: No such file or directory`.

- [x] **Step 3: Add the model types**

Apply the `models.hpp` additions from the Interfaces block above verbatim: `LoadedModule`, `ModprobeInfo`, `DeviceKey`, `DisabledDeviceEntry` after the `Driver` struct; `std::optional<std::string> signer;` inside `Driver` after `isSigned`; `#include <cstdint>` is already present. Add `struct ModulesChangedEvent {};` at the end of `events.hpp` (before the closing namespace).

- [x] **Step 4: Implement device_key**

Create `core/include/devmgr/services/device_key.hpp` exactly as in the Interfaces block. Create `core/src/device_key.cpp`:

```cpp
#include "devmgr/services/device_key.hpp"

#include <algorithm>

namespace devmgr::services {
namespace {
std::string lastSegment(const std::string& path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}
}  // namespace

std::string keyBusString(core::BusType bus) {
    switch (bus) {
        case core::BusType::Usb:
            return "usb";
        case core::BusType::Pci:
            return "pci";
        case core::BusType::Platform:
            return "platform";
        case core::BusType::Virtio:
            return "virtio";
        case core::BusType::Other:
            return "other";
    }
    return "other";
}

std::string positionFor(core::BusType /*bus*/, const std::string& sysfsPath) {
    // The last segment is the kernel's positional name on every bus we key:
    // USB port chain ("2-1.4"), PCI address ("0000:03:00.0"), platform name.
    return lastSegment(sysfsPath);
}

core::DeviceKey makeDeviceKey(const core::Device& device) {
    return core::DeviceKey{.bus = keyBusString(device.bus),
                           .vendorId = device.vendorId,
                           .productId = device.productId,
                           .serial = device.serial,
                           .position = positionFor(device.bus, device.sysfsPath)};
}

core::DeviceKey makeDeviceKey(const core::Device& device,
                              const std::vector<core::Device>& present) {
    auto key = makeDeviceKey(device);
    if (key.serial.empty()) return key;
    const bool cloned = std::any_of(present.begin(), present.end(), [&](const core::Device& d) {
        return d.sysfsPath != device.sysfsPath && keyBusString(d.bus) == key.bus &&
               d.vendorId == key.vendorId && d.productId == key.productId &&
               d.serial == key.serial;
    });
    if (cloned) key.serial.clear();  // downgrade to positional (spec §5.1)
    return key;
}

bool matchesDevice(const core::DeviceKey& key, const core::Device& device) {
    if (keyBusString(device.bus) != key.bus) return false;
    if (!key.serial.empty()) {
        return device.vendorId == key.vendorId && device.productId == key.productId &&
               device.serial == key.serial;
    }
    return positionFor(device.bus, device.sysfsPath) == key.position &&
           device.vendorId == key.vendorId && device.productId == key.productId;
}

}  // namespace devmgr::services
```

Add `src/device_key.cpp` to `core/CMakeLists.txt`'s source list.

- [x] **Step 5: Apply the PAL interface v2 + mechanical ripple (tree must stay green)**

1. Replace the three interfaces in `pal/interfaces.hpp` with the v2 bodies from the Interfaces block; add `lockdownMode` to `ISystemInfo::Info`.
2. `SysfsDeviceController` — change signature only for now (full v2 behavior is Task 3): header `setEnabled(const std::string&, bool, const std::string& rebindDriverHint)` returning `core::Result<std::optional<std::string>>`; declare `bindDriver`/`unbindDriver`. In the .cpp: adapt the existing body to `return std::optional<std::string>{};` on success (ignore the hint), and add stubs:

```cpp
core::Result<void> SysfsDeviceController::bindDriver(const std::string&, const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "bindDriver arrives in Task 3");
}
core::Result<void> SysfsDeviceController::unbindDriver(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "unbindDriver arrives in Task 3");
}
```

3. `daemon/src/request_processor.cpp` line 45: `return controller_.setEnabled(canonical.string(), enabled, "");` and change nothing else — but the method now returns `Result<optional<string>>` while the verb returns `Result<void>`, so map it:

```cpp
    auto applied = controller_.setEnabled(canonical.string(), enabled, "");
    if (!applied) return tl::unexpected(applied.error());
    return {};
```

4. `tests/fakes/fake_pal.hpp` — implement the full v2 surface with scripting hooks later tasks use (COMPLETE replacement of the class):

```cpp
class FakePal final : public pal::IDeviceEnumerator,
                      public pal::IDeviceController,
                      public pal::IDriverManager,
                      public pal::ISystemInfo {
   public:
    void seedDevice(core::Device device) {
        enabled_[device.sysfsPath] = true;
        devices_.push_back(std::move(device));
    }
    void seedDriver(const std::string& sysfsPath, core::Driver driver) {
        drivers_[sysfsPath].push_back(std::move(driver));
    }
    void seedLoadedModule(core::LoadedModule m) { loaded_.push_back(std::move(m)); }
    bool enabled(const std::string& sysfsPath) const {
        const auto it = enabled_.find(sysfsPath);
        return it != enabled_.end() && it->second;
    }

    core::Result<std::vector<core::Device>> enumerate() override { return devices_; }

    core::Result<std::optional<std::string>> setEnabled(const std::string& sysfsPath, bool enabled,
                                                        const std::string& hint) override {
        setEnabledCalls.push_back({sysfsPath, enabled, hint});
        const auto it = enabled_.find(sysfsPath);
        if (it == enabled_.end())
            return core::makeError(core::Error::Code::NotFound, "no such device: " + sysfsPath);
        it->second = enabled;
        return unboundDriverResult;
    }
    core::Result<void> bindDriver(const std::string& sysfsPath,
                                  const std::string& driver) override {
        bindCalls.push_back({sysfsPath, driver});
        return nextVoid;
    }
    core::Result<void> unbindDriver(const std::string& sysfsPath) override {
        unbindCalls.push_back(sysfsPath);
        return nextVoid;
    }

    core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) override {
        const auto it = drivers_.find(device.sysfsPath);
        if (it == drivers_.end()) return std::vector<core::Driver>{};
        return it->second;
    }
    core::Result<void> loadModule(const std::string& name) override {
        loadedModules.push_back(name);
        return nextVoid;
    }
    core::Result<void> unloadModule(const std::string& name) override {
        unloadedModules.push_back(name);
        return nextVoid;
    }
    core::Result<std::vector<core::LoadedModule>> listLoadedModules() override { return loaded_; }
    core::Result<core::Driver> moduleInfo(const std::string& name) override {
        for (const auto& [path, list] : drivers_)
            for (const auto& d : list)
                if (d.name == name) return d;
        return core::makeError(core::Error::Code::NotFound, "module not found: " + name);
    }
    core::Result<core::ModprobeInfo> modprobeInfo(const std::string&) override {
        return modprobeResult;
    }
    core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) override {
        const auto it = moduleDevices_.find(name);
        if (it == moduleDevices_.end()) return std::vector<std::string>{};
        return it->second;
    }
    void seedModuleDevices(const std::string& name, std::vector<std::string> paths) {
        moduleDevices_[name] = std::move(paths);
    }

    core::Result<Info> query() override { return info; }

    struct SetEnabledCall {
        std::string sysfsPath;
        bool enabled;
        std::string hint;
    };
    struct BindCall {
        std::string sysfsPath;
        std::string driver;
    };
    std::vector<SetEnabledCall> setEnabledCalls;
    std::vector<BindCall> bindCalls;
    std::vector<std::string> unbindCalls;
    std::vector<std::string> loadedModules;
    std::vector<std::string> unloadedModules;
    core::Result<void> nextVoid = {};
    core::Result<std::optional<std::string>> unboundDriverResult = std::optional<std::string>{};
    core::ModprobeInfo modprobeResult{};
    Info info{"fake-os 1.0", "6.1.0-fake", false, false, "none"};

   private:
    std::vector<core::Device> devices_;
    std::unordered_map<std::string, bool> enabled_;
    std::unordered_map<std::string, std::vector<core::Driver>> drivers_;
    std::unordered_map<std::string, std::vector<std::string>> moduleDevices_;
    std::vector<core::LoadedModule> loaded_;
};
```

NOTE: `seedDriver` changed key from DeviceId to sysfsPath — update the two existing call sites found via `grep -rn "seedDriver" tests/`.

5. `tests/fakes/fake_privileged_channel.hpp` — extend (COMPLETE replacement):

```cpp
class FakePrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override {
        calls.push_back({device.sysfsPath, enabled});
        return next;
    }
    core::Result<void> loadModule(const std::string& name) override {
        moduleCalls.push_back("load:" + name);
        return next;
    }
    core::Result<void> unloadModule(const std::string& name) override {
        moduleCalls.push_back("unload:" + name);
        return next;
    }
    core::Result<void> bindDriver(const core::Device& device, const std::string& driver) override {
        moduleCalls.push_back("bind:" + device.sysfsPath + ":" + driver);
        return next;
    }
    core::Result<void> unbindDriver(const core::Device& device) override {
        moduleCalls.push_back("unbind:" + device.sysfsPath);
        return next;
    }
    core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() override {
        return disabledEntries;
    }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    std::vector<std::string> moduleCalls;
    core::Result<void> next = {};
    core::Result<std::vector<core::DisabledDeviceEntry>> disabledEntries =
        std::vector<core::DisabledDeviceEntry>{};
};
```

6. Fix `tests/unit/test_sysfs_device_controller.cpp` call sites: `setEnabled(path, v)` → `setEnabled(path, v, "")` and result checks — `EXPECT_TRUE(r.has_value())` still compiles. Fix any `FakePal::query()` brace-init in `tests/unit/test_fake_pal.cpp` if it asserts Info fields.

- [x] **Step 6: Build + full test suite**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all existing tests + 6 new DeviceKey tests PASS.

- [x] **Step 7: Format + tidy touched files**

Run: `clang-format -i core/src/device_key.cpp core/include/devmgr/services/device_key.hpp core/include/devmgr/core/models.hpp core/include/devmgr/core/events.hpp core/include/devmgr/pal/interfaces.hpp tests/fakes/fake_pal.hpp tests/fakes/fake_privileged_channel.hpp tests/unit/test_device_key.cpp platform/linux/src/sysfs_device_controller.cpp platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp daemon/src/request_processor.cpp && clang-format --dry-run --Werror $(git diff --name-only | grep -E '\.(hpp|cpp)$')`
Expected: no output (clean). Then clang-tidy on the two changed .cpp translation units if the host toolchain allows (container is authoritative).

- [x] **Step 8: Commit (USER runs)**

```bash
git add -A && git commit -m "feat(core): Phase 5 T1 — DeviceKey tiered identity, LoadedModule/ModprobeInfo/DisabledDeviceEntry models, PAL interface v2"
```

---

### Task 2: Module-unload guard — `evaluateModuleUnload`

**Files:**
- Modify: `core/include/devmgr/services/critical_device_guard.hpp`
- Modify: `core/src/critical_device_guard.cpp`
- Test: `tests/unit/test_critical_device_guard.cpp` (append)

**Interfaces:**
- Consumes: `pal::CriticalityFacts`, `services::evaluateDisable(facts, path)`, `GuardVerdict`.
- Produces (T7 + T10 advisory rely on this exact shape):

```cpp
// appended to critical_device_guard.hpp
struct ModuleUnloadFacts {
    std::vector<std::string> affectedDevicePaths;  // canonical sysfs paths bound via the module
    std::vector<std::string> holders;              // dependent modules (/sys/module/<m>/holders)
    long refCount = 0;
};

// Pure policy (spec §4.3, order matters): holders → refcount → per-device
// evaluateDisable. Authoritative in devmgrd, advisory in the frontends.
GuardVerdict evaluateModuleUnload(const pal::CriticalityFacts& facts,
                                  const ModuleUnloadFacts& module);
```

(Add `#include <vector>` to the header's includes.)

- [x] **Step 1: Write the failing tests** — append to `tests/unit/test_critical_device_guard.cpp`:

```cpp
TEST(EvaluateModuleUnload, CleanModuleIsAllowed) {
    const auto v = devmgr::services::evaluateModuleUnload({}, {});
    EXPECT_TRUE(v.allowed);
}

TEST(EvaluateModuleUnload, HoldersRefuseFirstWithNames) {
    devmgr::services::ModuleUnloadFacts m;
    m.holders = {"child_a", "child_b"};
    m.refCount = 2;
    const auto v = devmgr::services::evaluateModuleUnload({}, m);
    ASSERT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "in use by child_a, child_b");
}

TEST(EvaluateModuleUnload, NonZeroRefcountWithoutHoldersRefuses) {
    devmgr::services::ModuleUnloadFacts m;
    m.refCount = 3;
    const auto v = devmgr::services::evaluateModuleUnload({}, m);
    ASSERT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "in use (refcount 3)");
}

TEST(EvaluateModuleUnload, ModuleBackingRootDeviceRefusesWithGuardReason) {
    devmgr::pal::CriticalityFacts facts;
    facts.rootBackingPaths = {"/sys/devices/pci0000:00/0000:03:00.0/nvme/nvme0"};
    devmgr::services::ModuleUnloadFacts m;
    m.affectedDevicePaths = {"/sys/devices/pci0000:00/0000:03:00.0"};
    const auto v = devmgr::services::evaluateModuleUnload(facts, m);
    ASSERT_FALSE(v.allowed);
    // Prefix + the underlying evaluateDisable reason.
    EXPECT_EQ(v.reason.rfind("module backs a critical device: ", 0), 0U) << v.reason;
}

TEST(EvaluateModuleUnload, HarmlessBoundDevicesAllowed) {
    devmgr::pal::CriticalityFacts facts;
    facts.rootBackingPaths = {"/sys/devices/pci0000:00/0000:03:00.0/nvme/nvme0"};
    devmgr::services::ModuleUnloadFacts m;
    m.affectedDevicePaths = {"/sys/devices/pci0000:00/usb1/1-2"};  // unrelated device
    EXPECT_TRUE(devmgr::services::evaluateModuleUnload(facts, m).allowed);
}
```

- [x] **Step 2: Run to verify failure**

Run: `cmake --build --preset linux-debug 2>&1 | tail -3`
Expected: FAIL — `evaluateModuleUnload` not declared.

- [x] **Step 3: Implement** — append to `core/src/critical_device_guard.cpp`:

```cpp
GuardVerdict evaluateModuleUnload(const pal::CriticalityFacts& facts,
                                  const ModuleUnloadFacts& module) {
    if (!module.holders.empty()) {
        std::string names;
        for (const auto& h : module.holders) {
            if (!names.empty()) names += ", ";
            names += h;
        }
        return {.allowed = false, .reason = "in use by " + names};
    }
    if (module.refCount > 0)
        return {.allowed = false,
                .reason = "in use (refcount " + std::to_string(module.refCount) + ")"};
    for (const auto& path : module.affectedDevicePaths) {
        const auto verdict = evaluateDisable(facts, path);
        if (!verdict.allowed)
            return {.allowed = false, .reason = "module backs a critical device: " + verdict.reason};
    }
    return {};
}
```

(Add `#include <string>` if not present.) Header addition per the Interfaces block.

- [x] **Step 4: Run tests**

Run: `ctest --test-dir build/linux-debug -R CriticalDeviceGuard --output-on-failure` (after build)
Expected: PASS including the 5 new tests. Also full `ctest` green.

- [x] **Step 5: Format + Commit (USER runs)**

```bash
git add -A && git commit -m "feat(core): Phase 5 T2 — evaluateModuleUnload guard (holders -> refcount -> per-device criticality)"
```

---

### Task 3: SysfsDeviceController v2 — unbind/bind + driver_override rebind

**Files:**
- Modify: `platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp`
- Modify: `platform/linux/src/sysfs_device_controller.cpp`
- Test: `tests/unit/test_sysfs_device_controller.cpp` (append)

**Interfaces:**
- Consumes: T1's `IDeviceController` v2 signatures.
- Produces: the concrete controller T7/T8/T9 act through. Mechanism selection is attribute-driven: `authorized` attr present → USB mechanism; else driver unbind/probe. Sysfs layout consumed (all under injected `sysfsRoot`):
  - device dir: `<dev>/driver` (symlink to `<root>/bus/<bus>/drivers/<name>`), `<dev>/driver_override` (optional attr), `<dev>/subsystem` (symlink to `<root>/bus/<bus>`)
  - bus dir: `<root>/bus/<bus>/drivers_probe`, `<root>/bus/<bus>/drivers/<name>/{bind,unbind}`

- [x] **Step 1: Write the failing tests** — append to `tests/unit/test_sysfs_device_controller.cpp` (the file already builds a tmp fake sysfs tree; follow its existing fixture conventions; the helpers below are self-contained):

```cpp
namespace {
// Builds <root>/devices/pci0000:00/0000:03:00.0 bound to driver "virtio-pci"
// on bus "pci", with driver_override present. Returns the device path.
std::filesystem::path makePciDevice(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    const fs::path dev = root / "devices/pci0000:00/0000:03:00.0";
    const fs::path bus = root / "bus/pci";
    const fs::path drv = bus / "drivers/virtio-pci";
    fs::create_directories(dev);
    fs::create_directories(drv);
    std::ofstream(bus / "drivers_probe") << "";
    std::ofstream(drv / "bind") << "";
    std::ofstream(drv / "unbind") << "";
    std::ofstream(dev / "driver_override") << "";
    fs::create_directory_symlink(drv, dev / "driver");
    fs::create_directory_symlink(bus, dev / "subsystem");
    return dev;
}
std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

TEST_F(SysfsDeviceControllerTest, DisableWithoutAuthorizedUnbindsAndReportsDriver) {
    const auto dev = makePciDevice(root_);
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), false, "");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ(**r, "virtio-pci");  // mechanism "unbind", driver reported
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers/virtio-pci/unbind"), "0000:03:00.0");
}

TEST_F(SysfsDeviceControllerTest, DisableWithNoBoundDriverReportsEmptyUnbindMechanism) {
    const auto dev = makePciDevice(root_);
    std::filesystem::remove(dev / "driver");
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), false, "");
    ASSERT_TRUE(r.has_value());
    ASSERT_TRUE(r->has_value());
    EXPECT_EQ(**r, "");  // unbind mechanism, nothing was bound
}

TEST_F(SysfsDeviceControllerTest, EnableUsesDriverOverrideThenProbeThenClearsOverride) {
    const auto dev = makePciDevice(root_);
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), true, "virtio-pci");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    EXPECT_FALSE(r->has_value());  // enables report nullopt
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers_probe"), "0000:03:00.0");
    // Override written then cleared: last write is the empty clear.
    EXPECT_EQ(slurp(dev / "driver_override"), "");
}

TEST_F(SysfsDeviceControllerTest, EnableWithoutHintSkipsOverride) {
    const auto dev = makePciDevice(root_);
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(dev.string(), true, "");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers_probe"), "0000:03:00.0");
}

TEST_F(SysfsDeviceControllerTest, UsbAuthorizedPathStillWinsWhenAttrExists) {
    // The Phase 4 fixture device (with `authorized`) must keep using it even
    // if a driver symlink also exists.
    // Reuse the existing fixture device; just assert behavior is unchanged:
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.setEnabled(device_.string(), false, "");
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->has_value());  // authorized mechanism => nullopt
}

TEST_F(SysfsDeviceControllerTest, SurgicalUnbindWritesDeviceNameToDriverUnbind) {
    const auto dev = makePciDevice(root_);
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    ASSERT_TRUE(c.unbindDriver(dev.string()).has_value());
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers/virtio-pci/unbind"), "0000:03:00.0");
}

TEST_F(SysfsDeviceControllerTest, SurgicalUnbindWithoutDriverIsNotFound) {
    const auto dev = makePciDevice(root_);
    std::filesystem::remove(dev / "driver");
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.unbindDriver(dev.string());
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::NotFound);
}

TEST_F(SysfsDeviceControllerTest, SurgicalBindWritesDeviceNameToNamedDriverBind) {
    const auto dev = makePciDevice(root_);
    std::filesystem::remove(dev / "driver");
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    ASSERT_TRUE(c.bindDriver(dev.string(), "virtio-pci").has_value());
    EXPECT_EQ(slurp(root_ / "bus/pci/drivers/virtio-pci/bind"), "0000:03:00.0");
}

TEST_F(SysfsDeviceControllerTest, SurgicalBindToUnknownDriverIsNotFound) {
    const auto dev = makePciDevice(root_);
    devmgr::platform_linux::SysfsDeviceController c(root_.string());
    auto r = c.bindDriver(dev.string(), "no_such_driver");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::NotFound);
}
```

(Adjust fixture member names — `root_` / `device_` — to the file's actual ones; keep assertions identical.)

- [x] **Step 2: Run to verify failure**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R SysfsDeviceController --output-on-failure`
Expected: new tests FAIL (unbind path missing / Task 1 stubs return Unsupported).

- [x] **Step 3: Implement the v2 controller** — replace `sysfs_device_controller.cpp` body with:

```cpp
#include "devmgr/platform/linux/sysfs_device_controller.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {
bool isContained(const fs::path& canonicalPath, const fs::path& canonicalRoot) {
    const auto rel = canonicalPath.lexically_relative(canonicalRoot);
    return !rel.empty() && !rel.native().starts_with("..");
}

core::Result<void> writeAttr(const fs::path& attr, const std::string& value) {
    std::ofstream out(attr);
    if (!out)
        return core::makeError(core::Error::Code::Io, "cannot open " + attr.string() + ": " +
                                                          std::generic_category().message(errno));
    out << value;
    out.flush();
    if (!out) return core::makeError(core::Error::Code::Io, "write failed: " + attr.string());
    return {};
}

// <dev>/subsystem -> <root>/bus/<bus>; returns the bus directory.
core::Result<fs::path> busDirFor(const fs::path& device) {
    std::error_code ec;
    const fs::path bus = fs::weakly_canonical(device / "subsystem", ec);
    if (ec || !fs::is_directory(bus, ec))
        return core::makeError(core::Error::Code::Unsupported,
                               "no subsystem link at " + device.string());
    return bus;
}

std::optional<std::string> boundDriverName(const fs::path& device) {
    std::error_code ec;
    const fs::path link = device / "driver";
    if (!fs::is_symlink(link, ec)) return std::nullopt;
    const fs::path target = fs::read_symlink(link, ec);
    if (ec) return std::nullopt;
    return target.filename().string();
}
}  // namespace

SysfsDeviceController::SysfsDeviceController(std::string sysfsRoot)
    : sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<fs::path> SysfsDeviceController::canonicalDevice(const std::string& sysfsPath) const {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    if (ec || !isContained(canonical, root))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);
    return canonical;
}

core::Result<std::optional<std::string>> SysfsDeviceController::setEnabled(
    const std::string& sysfsPath, bool enabled, const std::string& rebindDriverHint) {
    auto device = canonicalDevice(sysfsPath);
    if (!device) return tl::unexpected(device.error());
    std::error_code ec;

    // Mechanism 1 (Phase 4, USB): the authorized attribute.
    const fs::path authorized = *device / "authorized";
    if (fs::exists(authorized, ec)) {
        auto w = writeAttr(authorized, enabled ? "1" : "0");
        if (!w) return tl::unexpected(w.error());
        return std::optional<std::string>{};  // nullopt => authorized mechanism
    }

    // Mechanism 2 (spec §5.4): driver unbind / targeted rebind.
    auto bus = busDirFor(*device);
    if (!bus) return tl::unexpected(bus.error());
    const std::string devName = device->filename().string();

    if (!enabled) {
        const auto driver = boundDriverName(*device);
        if (!driver) return std::optional<std::string>{""};  // nothing bound: no-op disable
        auto w = writeAttr(*bus / "drivers" / *driver / "unbind", devName);
        if (!w) return tl::unexpected(w.error());
        return std::optional<std::string>{*driver};
    }

    // Enable: driver_override (when hinted and supported) -> drivers_probe ->
    // ALWAYS clear the override, success or failure (spec: never sticky).
    const fs::path override = *device / "driver_override";
    const bool useOverride = !rebindDriverHint.empty() && fs::exists(override, ec);
    if (useOverride) {
        auto w = writeAttr(override, rebindDriverHint);
        if (!w) return tl::unexpected(w.error());
    }
    auto probe = writeAttr(*bus / "drivers_probe", devName);
    if (useOverride) {
        auto clear = writeAttr(override, "");  // scope-guard semantics
        if (probe && !clear) return tl::unexpected(clear.error());
    }
    if (!probe) return tl::unexpected(probe.error());
    return std::optional<std::string>{};
}

core::Result<void> SysfsDeviceController::unbindDriver(const std::string& sysfsPath) {
    auto device = canonicalDevice(sysfsPath);
    if (!device) return tl::unexpected(device.error());
    const auto driver = boundDriverName(*device);
    if (!driver)
        return core::makeError(core::Error::Code::NotFound,
                               "no driver bound at " + device->string());
    auto bus = busDirFor(*device);
    if (!bus) return tl::unexpected(bus.error());
    return writeAttr(*bus / "drivers" / *driver / "unbind", device->filename().string());
}

core::Result<void> SysfsDeviceController::bindDriver(const std::string& sysfsPath,
                                                     const std::string& driverName) {
    auto device = canonicalDevice(sysfsPath);
    if (!device) return tl::unexpected(device.error());
    auto bus = busDirFor(*device);
    if (!bus) return tl::unexpected(bus.error());
    std::error_code ec;
    const fs::path bindAttr = *bus / "drivers" / driverName / "bind";
    if (!fs::exists(bindAttr, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "no such driver on this bus: " + driverName);
    return writeAttr(bindAttr, device->filename().string());
}

}  // namespace devmgr::platform_linux
```

Header (`sysfs_device_controller.hpp`) — final shape:

```cpp
#pragma once
#include <filesystem>
#include <optional>
#include <string>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// Acts on canonical sysfs paths under an injectable root (tests use tmp trees).
// Mechanism selection is attribute-driven: `authorized` present => USB
// authorized mechanism; otherwise driver unbind + driver_override/drivers_probe
// rebind (spec §5.4).
class SysfsDeviceController final : public pal::IDeviceController {
   public:
    explicit SysfsDeviceController(std::string sysfsRoot = "/sys");
    core::Result<std::optional<std::string>> setEnabled(const std::string& sysfsPath, bool enabled,
                                                        const std::string& rebindDriverHint) override;
    core::Result<void> bindDriver(const std::string& sysfsPath,
                                  const std::string& driverName) override;
    core::Result<void> unbindDriver(const std::string& sysfsPath) override;

   private:
    core::Result<std::filesystem::path> canonicalDevice(const std::string& sysfsPath) const;
    std::string sysfsRoot_;
};

}  // namespace devmgr::platform_linux
```

- [x] **Step 4: Run tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all PASS (Phase 4 authorized cases + 9 new).

- [x] **Step 5: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(platform): Phase 5 T3 — SysfsDeviceController v2: unbind disable, driver_override targeted rebind, surgical bind/unbind"
```

---

### Task 4: StateStore — atomic persisted desired state

**Files:**
- Create: `daemon/include/devmgr/daemon/state_store.hpp`, `daemon/src/state_store.cpp`
- Modify: `daemon/CMakeLists.txt` (devmgrd_lib: add `src/state_store.cpp`, link `nlohmann_json::nlohmann_json`)
- Test: `tests/unit/test_state_store.cpp` (+ register in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `core::DisabledDeviceEntry`, `core::DeviceKey`, `services::matchesDevice`.
- Produces (T7/T8/T9 rely on):

```cpp
// state_store.hpp (namespace devmgr::daemon)
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"

namespace devmgr::daemon {

// /var/lib/devmgrd/state.json owner (spec §5.2). Only non-default state is
// stored; enable deletes the entry. Atomic writes (tmp+fsync+rename); corrupt
// file moved to state.json.bad and started empty. Thread-safe (internal mutex)
// — callers still serialize ACTIONS via the apply mutex; this lock only guards
// the entry list + file.
class StateStore {
   public:
    explicit StateStore(std::string dirPath);
    core::Result<void> load();  // missing file => empty store, success
    core::Result<void> upsert(const core::DisabledDeviceEntry& entry);  // keyed by entry.key
    core::Result<void> remove(const core::DeviceKey& key);
    core::Result<void> setGuardSuspended(const core::DeviceKey& key, bool suspended);
    core::Result<void> setLastSysfsPath(const core::DeviceKey& key, const std::string& path);
    std::vector<core::DisabledDeviceEntry> entries() const;
    std::optional<core::DisabledDeviceEntry> findFor(const core::Device& device) const;

   private:
    core::Result<void> save();  // callers hold mutex_
    std::string dir_;
    std::vector<core::DisabledDeviceEntry> entries_;
    mutable std::mutex mutex_;
};

}  // namespace devmgr::daemon
```

JSON schema (versioned):

```json
{"version": 1, "entries": [{"bus": "usb", "vendor_id": "046d", "product_id": "c52b",
  "serial": "AB12", "position": "2-1.4", "mechanism": "authorized", "last_driver": "",
  "last_sysfs_path": "/sys/...", "disabled_at_utc": 1780000000, "guard_suspended": false}]}
```

- [x] **Step 1: Write the failing tests** — create `tests/unit/test_state_store.cpp`:

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/daemon/state_store.hpp"

namespace fs = std::filesystem;
using devmgr::core::DeviceKey;
using devmgr::core::DisabledDeviceEntry;
using devmgr::daemon::StateStore;

namespace {
DisabledDeviceEntry entry(std::string serial, std::string position) {
    return DisabledDeviceEntry{
        .key = DeviceKey{.bus = "usb",
                         .vendorId = "046d",
                         .productId = "c52b",
                         .serial = std::move(serial),
                         .position = std::move(position)},
        .mechanism = "authorized",
        .lastDriver = "",
        .lastSysfsPath = "/sys/devices/pci0000:00/usb2/2-1",
        .disabledAtUtc = 1780000000,
        .guardSuspended = false};
}
}  // namespace

class StateStoreTest : public ::testing::Test {
   protected:
    fs::path dir_;
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("devmgr-store-" + std::string(
                    ::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }
};

TEST_F(StateStoreTest, RoundTripsEntriesAcrossInstances) {
    {
        StateStore store(dir_.string());
        ASSERT_TRUE(store.load().has_value());
        ASSERT_TRUE(store.upsert(entry("AB12", "2-1")).has_value());
    }
    StateStore reloaded(dir_.string());
    ASSERT_TRUE(reloaded.load().has_value());
    ASSERT_EQ(reloaded.entries().size(), 1U);
    EXPECT_EQ(reloaded.entries()[0], entry("AB12", "2-1"));
}

TEST_F(StateStoreTest, UpsertReplacesSameKeyAndRemoveDeletes) {
    StateStore store(dir_.string());
    ASSERT_TRUE(store.load().has_value());
    auto e = entry("AB12", "2-1");
    ASSERT_TRUE(store.upsert(e).has_value());
    e.mechanism = "unbind";
    ASSERT_TRUE(store.upsert(e).has_value());
    ASSERT_EQ(store.entries().size(), 1U);
    EXPECT_EQ(store.entries()[0].mechanism, "unbind");
    ASSERT_TRUE(store.remove(e.key).has_value());
    EXPECT_TRUE(store.entries().empty());
}

TEST_F(StateStoreTest, CorruptFileIsMovedAsideAndStoreStartsEmpty) {
    fs::create_directories(dir_);
    std::ofstream(dir_ / "state.json") << "{not json";
    StateStore store(dir_.string());
    ASSERT_TRUE(store.load().has_value());  // load succeeds (empty), evidence kept
    EXPECT_TRUE(store.entries().empty());
    EXPECT_TRUE(fs::exists(dir_ / "state.json.bad"));
}

TEST_F(StateStoreTest, GuardSuspensionAndPathUpdatesPersist) {
    StateStore store(dir_.string());
    ASSERT_TRUE(store.load().has_value());
    const auto e = entry("AB12", "2-1");
    ASSERT_TRUE(store.upsert(e).has_value());
    ASSERT_TRUE(store.setGuardSuspended(e.key, true).has_value());
    ASSERT_TRUE(store.setLastSysfsPath(e.key, "/sys/new/path").has_value());
    StateStore reloaded(dir_.string());
    ASSERT_TRUE(reloaded.load().has_value());
    EXPECT_TRUE(reloaded.entries()[0].guardSuspended);
    EXPECT_EQ(reloaded.entries()[0].lastSysfsPath, "/sys/new/path");
}

TEST_F(StateStoreTest, FindForMatchesBySerialTupleOrLastPath) {
    StateStore store(dir_.string());
    ASSERT_TRUE(store.load().has_value());
    ASSERT_TRUE(store.upsert(entry("AB12", "2-1")).has_value());
    devmgr::core::Device d;
    d.bus = devmgr::core::BusType::Usb;
    d.vendorId = "046d";
    d.productId = "c52b";
    d.serial = "AB12";
    d.sysfsPath = "/sys/devices/pci0000:00/usb1/1-9";  // different port: serial tier wins
    ASSERT_TRUE(store.findFor(d).has_value());
    d.serial = "OTHER";
    EXPECT_FALSE(store.findFor(d).has_value());
    d.sysfsPath = "/sys/devices/pci0000:00/usb2/2-1";  // lastSysfsPath fallback
    EXPECT_TRUE(store.findFor(d).has_value());
}
```

Register `unit/test_state_store.cpp` in `tests/CMakeLists.txt` inside the `if(UNIX AND NOT APPLE)` block (it links `devmgrd_lib`).

- [x] **Step 2: Run to verify failure**

Run: `cmake --build --preset linux-debug 2>&1 | tail -3`
Expected: FAIL — `devmgr/daemon/state_store.hpp` missing.

- [x] **Step 3: Implement** — create `daemon/src/state_store.cpp`:

```cpp
#include "devmgr/daemon/state_store.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

#include <nlohmann/json.hpp>

#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
json toJson(const core::DisabledDeviceEntry& e) {
    return json{{"bus", e.key.bus},
                {"vendor_id", e.key.vendorId},
                {"product_id", e.key.productId},
                {"serial", e.key.serial},
                {"position", e.key.position},
                {"mechanism", e.mechanism},
                {"last_driver", e.lastDriver},
                {"last_sysfs_path", e.lastSysfsPath},
                {"disabled_at_utc", e.disabledAtUtc},
                {"guard_suspended", e.guardSuspended}};
}

core::DisabledDeviceEntry fromJson(const json& j) {
    core::DisabledDeviceEntry e;
    e.key = core::DeviceKey{.bus = j.at("bus"),
                            .vendorId = j.at("vendor_id"),
                            .productId = j.at("product_id"),
                            .serial = j.at("serial"),
                            .position = j.at("position")};
    e.mechanism = j.at("mechanism");
    e.lastDriver = j.at("last_driver");
    e.lastSysfsPath = j.at("last_sysfs_path");
    e.disabledAtUtc = j.at("disabled_at_utc");
    e.guardSuspended = j.at("guard_suspended");
    return e;
}
}  // namespace

StateStore::StateStore(std::string dirPath) : dir_(std::move(dirPath)) {}

core::Result<void> StateStore::load() {
    const std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    const fs::path file = fs::path(dir_) / "state.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) return {};
    std::ifstream in(file);
    json doc = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entries")) {
        // Never silently destroy evidence (spec §5.2).
        fs::rename(file, fs::path(dir_) / "state.json.bad", ec);
        return {};
    }
    for (const auto& j : doc["entries"]) entries_.push_back(fromJson(j));
    return {};
}

core::Result<void> StateStore::save() {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    json doc{{"version", 1}, {"entries", json::array()}};
    for (const auto& e : entries_) doc["entries"].push_back(toJson(e));
    const fs::path file = fs::path(dir_) / "state.json";
    const fs::path tmp = fs::path(dir_) / "state.json.tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) return core::makeError(core::Error::Code::Io, "cannot write " + tmp.string());
        out << doc.dump(2);
        out.flush();
        if (!out) return core::makeError(core::Error::Code::Io, "write failed: " + tmp.string());
    }
    fs::rename(tmp, file, ec);  // atomic on POSIX
    if (ec) return core::makeError(core::Error::Code::Io, "rename failed: " + ec.message());
    return {};
}

core::Result<void> StateStore::upsert(const core::DisabledDeviceEntry& entry) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.key == entry.key) {
            e = entry;
            return save();
        }
    }
    entries_.push_back(entry);
    return save();
}

core::Result<void> StateStore::remove(const core::DeviceKey& key) {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(entries_, [&](const auto& e) { return e.key == key; });
    return save();
}

core::Result<void> StateStore::setGuardSuspended(const core::DeviceKey& key, bool suspended) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.key == key) {
            e.guardSuspended = suspended;
            return save();
        }
    }
    return core::makeError(core::Error::Code::NotFound, "no entry for key");
}

core::Result<void> StateStore::setLastSysfsPath(const core::DeviceKey& key,
                                                const std::string& path) {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (auto& e : entries_) {
        if (e.key == key) {
            e.lastSysfsPath = path;
            return save();
        }
    }
    return core::makeError(core::Error::Code::NotFound, "no entry for key");
}

std::vector<core::DisabledDeviceEntry> StateStore::entries() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

std::optional<core::DisabledDeviceEntry> StateStore::findFor(const core::Device& device) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& e : entries_) {
        if (services::matchesDevice(e.key, device) || e.lastSysfsPath == device.sysfsPath)
            return e;
    }
    return std::nullopt;
}

}  // namespace devmgr::daemon
```

`daemon/CMakeLists.txt` — devmgrd_lib block becomes:

```cmake
find_package(nlohmann_json CONFIG REQUIRED)
add_library(devmgrd_lib STATIC
    src/request_processor.cpp
    src/state_store.cpp)
target_include_directories(devmgrd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgrd_lib PUBLIC devmgr_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(devmgrd_lib PUBLIC cxx_std_20)
```

- [x] **Step 4: Run tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R StateStore --output-on-failure`
Expected: 5 PASS; full suite green.

- [x] **Step 5: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(daemon): Phase 5 T4 — StateStore: atomic persisted desired state, corruption sidecar, tiered-key lookup"
```

---

### Task 5: KmodDriverManager — read side (fixture-testable, no real .ko)

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/kmod_driver_manager.hpp`, `platform/linux/src/kmod_driver_manager.cpp`
- Modify: `platform/linux/CMakeLists.txt`
- Test: `tests/unit/test_kmod_driver_manager.cpp` (+ register in `tests/CMakeLists.txt`, `if(UNIX AND NOT APPLE)` block)

**Interfaces:**
- Consumes: T1 `IDriverManager` v2, `core::Driver/LoadedModule/ModprobeInfo`.
- Produces: the concrete manager used by frontends (reads) and devmgrd (writes, Task 6). Header is libkmod-free (pimpl); `<libkmod.h>` only in the .cpp:

```cpp
// kmod_driver_manager.hpp (namespace devmgr::platform_linux)
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// libkmod-backed IDriverManager (spec §7.1). Reads are unprivileged; the write
// methods (Task 6) need CAP_SYS_MODULE — daemon-only callers. libkmod stays
// quarantined in the .cpp (libudev pattern).
class KmodDriverManager final : public pal::IDriverManager {
   public:
    struct Options {
        std::string sysfsRoot = "/sys";
        std::string moduleDir;  // "" => kernel default (/lib/modules/`uname -r`)
        std::optional<std::vector<std::string>> configPaths;  // nullopt => system modprobe.d
        std::string securityDir = "/sys/kernel/security";     // lockdown, for load errors
    };
    explicit KmodDriverManager(Options options = {});
    ~KmodDriverManager() override;
    KmodDriverManager(const KmodDriverManager&) = delete;
    KmodDriverManager& operator=(const KmodDriverManager&) = delete;
    KmodDriverManager(KmodDriverManager&&) noexcept = default;  // declared dtor would
    KmodDriverManager& operator=(KmodDriverManager&&) noexcept = default;  // suppress these

    // First element = the bound (or builtin) driver's module when resolvable;
    // the rest are modalias candidates (spec §7.1 / bind-dropdown data).
    core::Result<std::vector<core::Driver>> driversFor(const core::Device& device) override;
    core::Result<void> loadModule(const std::string& name) override;      // Task 6
    core::Result<void> unloadModule(const std::string& name) override;    // Task 6
    core::Result<std::vector<core::LoadedModule>> listLoadedModules() override;
    core::Result<core::Driver> moduleInfo(const std::string& name) override;
    core::Result<core::ModprobeInfo> modprobeInfo(const std::string& name) override;
    core::Result<std::vector<std::string>> devicesUsingModule(const std::string& name) override;

   private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace devmgr::platform_linux
```

- [x] **Step 1: Write the failing tests** — create `tests/unit/test_kmod_driver_manager.cpp`. Fixtures are PLAIN TEXT: libkmod falls back to text `modules.dep`/`modules.alias` when no `.bin` indexes exist, and `configPaths` accepts fixture modprobe.d dirs — no real `.ko` needed for lookup/dep/config behavior.

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/kmod_driver_manager.hpp"

namespace fs = std::filesystem;
using devmgr::platform_linux::KmodDriverManager;

class KmodDriverManagerTest : public ::testing::Test {
   protected:
    fs::path root_;       // fake sysfs
    fs::path moddir_;     // fake /lib/modules/<ver>
    fs::path confdir_;    // fake modprobe.d

    void SetUp() override {
        const auto base = fs::temp_directory_path() /
                          ("devmgr-kmod-" + std::string(::testing::UnitTest::GetInstance()
                                                            ->current_test_info()
                                                            ->name()));
        fs::remove_all(base);
        root_ = base / "sys";
        moddir_ = base / "modules";
        confdir_ = base / "modprobe.d";
        fs::create_directories(root_);
        fs::create_directories(moddir_ / "kernel/drivers/net");
        fs::create_directories(confdir_);
        // Text indexes (no .bin present => libkmod parses these).
        std::ofstream(moddir_ / "modules.dep")
            << "kernel/drivers/net/dummy.ko: kernel/drivers/net/helper.ko\n"
               "kernel/drivers/net/helper.ko:\n";
        std::ofstream(moddir_ / "modules.alias")
            << "alias usb:v046DpC52B* dummy\n";
        std::ofstream(moddir_ / "modules.symbols") << "";
        std::ofstream(moddir_ / "modules.builtin") << "";
        std::ofstream(confdir_ / "test.conf")
            << "options dummy numdummies=2\n"
               "blacklist evil\n"
               "install shady /bin/false\n";
    }
    void TearDown() override { fs::remove_all(root_.parent_path()); }

    KmodDriverManager make() {
        KmodDriverManager::Options o;
        o.sysfsRoot = root_.string();
        o.moduleDir = moddir_.string();
        o.configPaths = std::vector<std::string>{confdir_.string()};
        return KmodDriverManager(std::move(o));
    }
};

TEST_F(KmodDriverManagerTest, ModaliasLookupYieldsCandidateWithDependencies) {
    auto mgr = make();
    devmgr::core::Device d;
    d.modalias = "usb:v046DpC52Bd1101";
    d.sysfsPath = (root_ / "devices/usb1/1-2").string();
    auto r = mgr.driversFor(d);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_FALSE(r->empty());
    EXPECT_EQ(r->front().name, "dummy");
    ASSERT_EQ(r->front().dependencies.size(), 1U);
    EXPECT_EQ(r->front().dependencies[0], "helper");
}

TEST_F(KmodDriverManagerTest, BoundDriverResolvedViaSysfsComesFirst) {
    // <dev>/driver -> <root>/bus/usb/drivers/usbhid; driver/module -> module dir name.
    const fs::path dev = root_ / "devices/usb1/1-2";
    const fs::path drv = root_ / "bus/usb/drivers/usbhid";
    const fs::path mod = root_ / "module/usbhid";
    fs::create_directories(dev);
    fs::create_directories(drv);
    fs::create_directories(mod);
    fs::create_directory_symlink(drv, dev / "driver");
    fs::create_directory_symlink(mod, drv / "module");
    auto mgr = make();
    devmgr::core::Device d;
    d.sysfsPath = dev.string();
    d.modalias = "usb:v046DpC52Bd1101";
    d.boundDriver = "usbhid";
    auto r = mgr.driversFor(d);
    ASSERT_TRUE(r.has_value());
    ASSERT_GE(r->size(), 2U);
    EXPECT_EQ(r->front().name, "usbhid");  // bound first
    EXPECT_EQ((*r)[1].name, "dummy");      // then candidates
}

TEST_F(KmodDriverManagerTest, BuiltinDriverDetectedWhenNoModuleLink) {
    const fs::path dev = root_ / "devices/platform/gpio-keys";
    const fs::path drv = root_ / "bus/platform/drivers/gpio_keys";
    fs::create_directories(dev);
    fs::create_directories(drv);  // NO module link => builtin
    fs::create_directory_symlink(drv, dev / "driver");
    auto mgr = make();
    devmgr::core::Device d;
    d.sysfsPath = dev.string();
    auto r = mgr.driversFor(d);
    ASSERT_TRUE(r.has_value());
    ASSERT_FALSE(r->empty());
    EXPECT_EQ(r->front().name, "gpio_keys");
    EXPECT_EQ(r->front().kind, devmgr::core::DriverKind::Builtin);
}

TEST_F(KmodDriverManagerTest, ModprobeInfoReadsOptionsAndBlacklistFromFixtureConf) {
    auto mgr = make();
    auto dummy = mgr.modprobeInfo("dummy");
    ASSERT_TRUE(dummy.has_value()) << dummy.error().message;
    ASSERT_TRUE(dummy->options.has_value());
    EXPECT_EQ(*dummy->options, "numdummies=2");
    EXPECT_FALSE(dummy->blacklisted);
    auto evil = mgr.modprobeInfo("evil");
    ASSERT_TRUE(evil.has_value());
    EXPECT_TRUE(evil->blacklisted);
}

TEST_F(KmodDriverManagerTest, DevicesUsingModuleWalksModuleDriversToDevices) {
    // /sys/module/usbhid/drivers/usb:usbhid -> driver dir; driver dir has a
    // device symlink "1-2:1.0" -> the device under /sys/devices.
    const fs::path dev = root_ / "devices/usb1/1-2/1-2:1.0";
    const fs::path drv = root_ / "bus/usb/drivers/usbhid";
    fs::create_directories(dev);
    fs::create_directories(drv);
    fs::create_directory_symlink(dev, drv / "1-2:1.0");
    std::ofstream(drv / "uevent") << "";  // non-symlink entries must be skipped
    fs::create_directories(root_ / "module/usbhid/drivers");
    fs::create_directory_symlink(drv, root_ / "module/usbhid/drivers/usb:usbhid");
    auto mgr = make();
    auto r = mgr.devicesUsingModule("usbhid");
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0], fs::weakly_canonical(dev).string());
}

TEST_F(KmodDriverManagerTest, ModuleInfoUnknownNameIsNotFound) {
    auto mgr = make();
    auto r = mgr.moduleInfo("no_such_module_xyz");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::NotFound);
}
```

- [x] **Step 2: Run to verify failure**

Run: `cmake --build --preset linux-debug 2>&1 | tail -3`
Expected: FAIL — header missing.

- [x] **Step 3: CMake for libkmod** — in `platform/linux/CMakeLists.txt` after the UDEV pkg check:

```cmake
pkg_check_modules(KMOD REQUIRED IMPORTED_TARGET libkmod)   # -> PkgConfig::KMOD
```

add `src/kmod_driver_manager.cpp` to `devmgr_pal_linux`'s sources and `PkgConfig::KMOD` to its PRIVATE link libraries.

- [x] **Step 4: Implement the read side** — create `platform/linux/src/kmod_driver_manager.cpp`:

```cpp
#include "devmgr/platform/linux/kmod_driver_manager.hpp"

#include <libkmod.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {
// Kernel treats '-' and '_' as equivalent in module names.
std::string normalized(std::string name) {
    std::replace(name.begin(), name.end(), '-', '_');
    return name;
}
}  // namespace

struct KmodDriverManager::Impl {
    Options options;
    kmod_ctx* ctx = nullptr;

    explicit Impl(Options o) : options(std::move(o)) {
        std::vector<const char*> paths;
        const char* const* configPaths = nullptr;
        if (options.configPaths) {
            for (const auto& p : *options.configPaths) paths.push_back(p.c_str());
            paths.push_back(nullptr);
            configPaths = paths.data();
        }
        ctx = kmod_new(options.moduleDir.empty() ? nullptr : options.moduleDir.c_str(),
                       configPaths);
    }
    ~Impl() {
        if (ctx != nullptr) kmod_unref(ctx);
    }

    core::Result<void> ready() const {
        if (ctx == nullptr)
            return core::makeError(core::Error::Code::Io, "libkmod context creation failed");
        return {};
    }

    // Fills a core::Driver from one kmod_module (does NOT take ownership).
    core::Driver toDriver(kmod_module* mod) const {
        core::Driver d;
        d.name = kmod_module_get_name(mod);
        const char* path = kmod_module_get_path(mod);
        d.path = path != nullptr ? path : "";
        const int state = kmod_module_get_initstate(mod);
        d.loaded = state == KMOD_MODULE_LIVE || state == KMOD_MODULE_COMING;
        d.kind = state == KMOD_MODULE_BUILTIN ? core::DriverKind::Builtin
                                              : core::DriverKind::KernelModule;
        kmod_list* deps = kmod_module_get_dependencies(mod);
        kmod_list* it = nullptr;
        kmod_list_foreach(it, deps) {
            kmod_module* dep = kmod_module_get_module(it);
            d.dependencies.emplace_back(kmod_module_get_name(dep));
            kmod_module_unref(dep);
        }
        kmod_module_unref_list(deps);
        // Signature + version come from the .ko's modinfo section; absent for
        // builtins and for fixture indexes without real files — stay defaults.
        kmod_list* info = nullptr;
        if (kmod_module_get_info(mod, &info) >= 0) {
            kmod_list* i = nullptr;
            kmod_list_foreach(i, info) {
                const char* key = kmod_module_info_get_key(i);
                const char* value = kmod_module_info_get_value(i);
                if (key == nullptr || value == nullptr) continue;
                if (std::strcmp(key, "version") == 0) d.version = value;
                if (std::strcmp(key, "signer") == 0) {
                    d.isSigned = true;
                    d.signer = value;
                }
                if (std::strcmp(key, "sig_id") == 0) d.isSigned = true;
            }
            kmod_module_free_info(info);
        }
        return d;
    }

    core::Result<kmod_module*> byName(const std::string& name) {
        if (auto r = ready(); !r) return tl::unexpected(r.error());
        kmod_list* list = nullptr;
        const int err = kmod_module_new_from_lookup(ctx, name.c_str(), &list);
        if (err < 0 || list == nullptr)
            return core::makeError(core::Error::Code::NotFound, "module not found: " + name);
        kmod_module* mod = kmod_module_get_module(list);  // first match
        kmod_module_unref_list(list);
        return mod;  // caller unrefs
    }
};

KmodDriverManager::KmodDriverManager(Options options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}
KmodDriverManager::~KmodDriverManager() = default;

core::Result<std::vector<core::Driver>> KmodDriverManager::driversFor(
    const core::Device& device) {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    std::vector<core::Driver> out;

    // Bound driver first: <dev>/driver -> driver dir; driver/module -> module.
    std::error_code ec;
    const fs::path driverLink = fs::path(device.sysfsPath) / "driver";
    if (fs::is_symlink(driverLink, ec)) {
        const fs::path driverDir = fs::weakly_canonical(driverLink, ec);
        const std::string driverName = driverDir.filename().string();
        const fs::path moduleLink = driverDir / "module";
        if (fs::is_symlink(moduleLink, ec)) {
            const std::string moduleName =
                fs::read_symlink(moduleLink, ec).filename().string();
            if (auto mod = impl_->byName(moduleName); mod) {
                out.push_back(impl_->toDriver(*mod));
                kmod_module_unref(*mod);
            }
        } else {  // driver with no module: built into the kernel
            core::Driver builtin;
            builtin.name = driverName;
            builtin.kind = core::DriverKind::Builtin;
            builtin.loaded = true;
            out.push_back(std::move(builtin));
        }
    }

    // Then modalias candidates — the exact resolution modprobe performs.
    if (!device.modalias.empty()) {
        kmod_list* list = nullptr;
        if (kmod_module_new_from_lookup(impl_->ctx, device.modalias.c_str(), &list) >= 0) {
            kmod_list* it = nullptr;
            kmod_list_foreach(it, list) {
                kmod_module* mod = kmod_module_get_module(it);
                auto d = impl_->toDriver(mod);
                kmod_module_unref(mod);
                const bool dup = std::any_of(out.begin(), out.end(), [&](const core::Driver& e) {
                    return normalized(e.name) == normalized(d.name);
                });
                if (!dup) out.push_back(std::move(d));
            }
            kmod_module_unref_list(list);
        }
    }
    return out;
}

core::Result<std::vector<core::LoadedModule>> KmodDriverManager::listLoadedModules() {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    kmod_list* list = nullptr;
    if (kmod_module_new_from_loaded(impl_->ctx, &list) < 0)
        return core::makeError(core::Error::Code::Io, "cannot read loaded modules");
    std::vector<core::LoadedModule> out;
    kmod_list* it = nullptr;
    kmod_list_foreach(it, list) {
        kmod_module* mod = kmod_module_get_module(it);
        core::LoadedModule m;
        m.name = kmod_module_get_name(mod);
        m.sizeBytes = static_cast<std::uint64_t>(kmod_module_get_size(mod));
        m.refCount = kmod_module_get_refcnt(mod);
        kmod_list* holders = kmod_module_get_holders(mod);
        kmod_list* h = nullptr;
        kmod_list_foreach(h, holders) {
            kmod_module* hm = kmod_module_get_module(h);
            m.holders.emplace_back(kmod_module_get_name(hm));
            kmod_module_unref(hm);
        }
        kmod_module_unref_list(holders);
        kmod_module_unref(mod);
        out.push_back(std::move(m));
    }
    kmod_module_unref_list(list);
    return out;
}

core::Result<core::Driver> KmodDriverManager::moduleInfo(const std::string& name) {
    auto mod = impl_->byName(name);
    if (!mod) return tl::unexpected(mod.error());
    auto d = impl_->toDriver(*mod);
    kmod_module_unref(*mod);
    return d;
}

core::Result<core::ModprobeInfo> KmodDriverManager::modprobeInfo(const std::string& name) {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    core::ModprobeInfo info;
    const std::string wanted = normalized(name);
    if (kmod_config_iter* it = kmod_config_get_options(impl_->ctx)) {
        while (kmod_config_iter_next(it)) {
            const char* key = kmod_config_iter_get_key(it);
            const char* value = kmod_config_iter_get_value(it);
            if (key != nullptr && normalized(key) == wanted && value != nullptr) {
                info.options = info.options ? *info.options + " " + value : std::string(value);
            }
        }
        kmod_config_iter_free_iter(it);
    }
    if (kmod_config_iter* it = kmod_config_get_blacklists(impl_->ctx)) {
        while (kmod_config_iter_next(it)) {
            const char* key = kmod_config_iter_get_key(it);
            if (key != nullptr && normalized(key) == wanted) info.blacklisted = true;
        }
        kmod_config_iter_free_iter(it);
    }
    return info;
}

core::Result<std::vector<std::string>> KmodDriverManager::devicesUsingModule(
    const std::string& name) {
    std::error_code ec;
    std::vector<std::string> out;
    const fs::path driversDir = fs::path(impl_->options.sysfsRoot) / "module" / name / "drivers";
    if (!fs::is_directory(driversDir, ec)) return out;  // not loaded / no drivers: empty
    for (const auto& drv : fs::directory_iterator(driversDir, ec)) {
        const fs::path driverDir = fs::weakly_canonical(drv.path(), ec);
        if (ec) continue;
        for (const auto& entry : fs::directory_iterator(driverDir, ec)) {
            if (!fs::is_symlink(entry.path(), ec)) continue;
            const fs::path target = fs::weakly_canonical(entry.path(), ec);
            if (ec) continue;
            // Device links point under .../devices/; skip module/bind/uevent etc.
            if (target.string().find("/devices/") == std::string::npos) continue;
            out.push_back(target.string());
        }
    }
    return out;
}

// loadModule / unloadModule arrive in Task 6.
core::Result<void> KmodDriverManager::loadModule(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "loadModule arrives in Task 6");
}
core::Result<void> KmodDriverManager::unloadModule(const std::string&) {
    return core::makeError(core::Error::Code::Unsupported, "unloadModule arrives in Task 6");
}

}  // namespace devmgr::platform_linux
```

- [x] **Step 5: Run tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R KmodDriverManager --output-on-failure`
Expected: 6 PASS. NOTE: if `ModaliasLookupYieldsCandidateWithDependencies` fails because libkmod insists on binary indexes on your kmod version, run `depmod -b <fixture-base> <ver>`-style generation is NOT available for fixtures — instead check `kmod --version` ≥ 28 (text fallback is long-standing); investigate before proceeding, do not skip the test.

- [x] **Step 6: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(platform): Phase 5 T5 — KmodDriverManager read side: modalias candidates, bound/builtin resolution, modprobe.d read-only, module->devices walk"
```

---

### Task 6: KmodDriverManager write side + load-failure taxonomy + LinuxSystemInfo

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/kmod_error_taxonomy.hpp` (pure, header-only)
- Create: `platform/linux/include/devmgr/platform/linux/linux_system_info.hpp`, `platform/linux/src/linux_system_info.cpp`
- Modify: `platform/linux/src/kmod_driver_manager.cpp` (replace the two Task 5 stubs)
- Modify: `platform/linux/CMakeLists.txt` (add `src/linux_system_info.cpp`)
- Test: `tests/unit/test_kmod_error_taxonomy.cpp`, `tests/unit/test_linux_system_info.cpp` (+ register both)

**Interfaces:**
- Produces:

```cpp
// kmod_error_taxonomy.hpp (namespace devmgr::platform_linux) — pure functions,
// no libkmod include, fully unit-testable (spec §8.1 taxonomy).
#pragma once
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"

namespace devmgr::platform_linux {

struct DepState {
    std::string name;
    bool loaded = false;
};

// Maps a failed insert (positive errno `err`) to the user-facing error.
// `deps` lets dependency failures be named: the first unloaded dependency is
// reported as the culprit (spec: dependency failures bubble up named).
inline core::Error describeLoadFailure(int err, const std::string& module,
                                       const std::vector<DepState>& deps,
                                       const std::string& lockdownMode) {
    const auto culprit = [&]() -> std::string {
        for (const auto& d : deps)
            if (!d.loaded) return d.name;
        return {};
    }();
    const std::string subject =
        culprit.empty() ? "module '" + module + "'" : "dependency '" + culprit + "'";
    if (err == EKEYREJECTED || err == ENOKEY || err == EPERM)
        return {core::Error::Code::Permission,
                subject + " rejected: unsigned module (Secure Boot / lockdown: " + lockdownMode +
                    ")"};
    if (err == ENOENT)
        return {core::Error::Code::NotFound, subject + " not found for this kernel"};
    if (err == EBUSY) return {core::Error::Code::Busy, subject + " is busy"};
    return {core::Error::Code::Io,
            "loading " + subject + " failed: " + std::generic_category().message(err)};
}

inline core::Error describeUnloadFailure(int err, const std::string& module,
                                         const std::vector<std::string>& holders) {
    if (err == EBUSY) {
        std::string names;
        for (const auto& h : holders) {
            if (!names.empty()) names += ", ";
            names += h;
        }
        return {core::Error::Code::Busy,
                "module '" + module + "' is in use" + (names.empty() ? "" : " by " + names)};
    }
    if (err == EPERM)
        return {core::Error::Code::Permission,
                "unload of '" + module + "' rejected by the kernel (lockdown?)"};
    if (err == ENOENT) return {core::Error::Code::NotFound, "module '" + module + "' not loaded"};
    return {core::Error::Code::Io,
            "unloading '" + module + "' failed: " + std::generic_category().message(err)};
}

}  // namespace devmgr::platform_linux
```

```cpp
// linux_system_info.hpp (namespace devmgr::platform_linux)
#pragma once
#include <string>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// Parsers exposed for tests + shared use (KmodDriverManager reads lockdown).
std::string readLockdownMode(const std::string& securityDir);  // "none" if absent
bool readSecureBoot(const std::string& efivarsDir);            // false if absent (BIOS)
std::string readPrettyName(const std::string& osReleasePath);

class LinuxSystemInfo final : public pal::ISystemInfo {
   public:
    struct Paths {
        std::string osRelease = "/etc/os-release";
        std::string efivarsDir = "/sys/firmware/efi/efivars";
        std::string securityDir = "/sys/kernel/security";
    };
    explicit LinuxSystemInfo(Paths paths = {});
    core::Result<Info> query() override;  // rebootPending stays false (Phase 6)

   private:
    Paths paths_;
};

}  // namespace devmgr::platform_linux
```

- [x] **Step 1: Write the failing tests**

`tests/unit/test_kmod_error_taxonomy.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cerrno>

#include "devmgr/platform/linux/kmod_error_taxonomy.hpp"

using devmgr::core::Error;
namespace pl = devmgr::platform_linux;

TEST(KmodErrorTaxonomy, SignatureRejectionNamesLockdownMode) {
    const auto e = pl::describeLoadFailure(EKEYREJECTED, "nvidia", {}, "integrity");
    EXPECT_EQ(e.code, Error::Code::Permission);
    EXPECT_EQ(e.message,
              "module 'nvidia' rejected: unsigned module (Secure Boot / lockdown: integrity)");
}

TEST(KmodErrorTaxonomy, UnloadedDependencyIsNamedAsCulprit) {
    const auto e = pl::describeLoadFailure(
        EKEYREJECTED, "parent", {{"dep_ok", true}, {"dep_bad", false}}, "none");
    EXPECT_NE(e.message.find("dependency 'dep_bad'"), std::string::npos) << e.message;
}

TEST(KmodErrorTaxonomy, EnoentIsNotFoundForThisKernel) {
    const auto e = pl::describeLoadFailure(ENOENT, "ghost", {}, "none");
    EXPECT_EQ(e.code, Error::Code::NotFound);
    EXPECT_EQ(e.message, "module 'ghost' not found for this kernel");
}

TEST(KmodErrorTaxonomy, UnloadBusyListsHolders) {
    const auto e = pl::describeUnloadFailure(EBUSY, "usbcore", {"usbhid", "xhci_hcd"});
    EXPECT_EQ(e.code, Error::Code::Busy);
    EXPECT_EQ(e.message, "module 'usbcore' is in use by usbhid, xhci_hcd");
}
```

`tests/unit/test_linux_system_info.cpp`:

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/linux_system_info.hpp"

namespace fs = std::filesystem;
namespace pl = devmgr::platform_linux;

class LinuxSystemInfoTest : public ::testing::Test {
   protected:
    fs::path dir_;
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("devmgr-sysinfo-" + std::string(::testing::UnitTest::GetInstance()
                                                    ->current_test_info()
                                                    ->name()));
        fs::create_directories(dir_ / "efivars");
        fs::create_directories(dir_ / "security");
    }
    void TearDown() override { fs::remove_all(dir_); }
};

TEST_F(LinuxSystemInfoTest, SecureBootReadsByteFourOfTheEfiVariable) {
    // 4-byte attribute header + value byte 0x01 = enabled.
    std::ofstream(dir_ / "efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c",
                  std::ios::binary)
        .write("\x06\x00\x00\x00\x01", 5);
    EXPECT_TRUE(pl::readSecureBoot((dir_ / "efivars").string()));
}

TEST_F(LinuxSystemInfoTest, NoEfivarsMeansBiosBootSecureBootOff) {
    EXPECT_FALSE(pl::readSecureBoot((dir_ / "no-such-dir").string()));
}

TEST_F(LinuxSystemInfoTest, LockdownParsesTheBracketedToken) {
    std::ofstream(dir_ / "security/lockdown") << "none [integrity] confidentiality\n";
    EXPECT_EQ(pl::readLockdownMode((dir_ / "security").string()), "integrity");
}

TEST_F(LinuxSystemInfoTest, MissingLockdownFileMeansNone) {
    EXPECT_EQ(pl::readLockdownMode((dir_ / "security").string()), "none");
}

TEST_F(LinuxSystemInfoTest, PrettyNameParsedFromOsRelease) {
    std::ofstream(dir_ / "os-release") << "NAME=Gentoo\nPRETTY_NAME=\"Gentoo Linux\"\n";
    EXPECT_EQ(pl::readPrettyName((dir_ / "os-release").string()), "Gentoo Linux");
}

TEST_F(LinuxSystemInfoTest, QueryFillsKernelVersionAndDefaults) {
    pl::LinuxSystemInfo::Paths p;
    p.osRelease = (dir_ / "missing").string();
    p.efivarsDir = (dir_ / "efivars").string();
    p.securityDir = (dir_ / "security").string();
    pl::LinuxSystemInfo info(p);
    auto r = info.query();
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(r->kernelVersion.empty());  // uname(2) always answers
    EXPECT_FALSE(r->rebootPending);          // honest Phase 6 stub
    EXPECT_EQ(r->lockdownMode, "none");
}
```

- [x] **Step 2: Run to verify failure** — build fails on missing headers.

- [x] **Step 3: Implement LinuxSystemInfo** — `platform/linux/src/linux_system_info.cpp`:

```cpp
#include "devmgr/platform/linux/linux_system_info.hpp"

#include <sys/utsname.h>

#include <filesystem>
#include <fstream>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

std::string readLockdownMode(const std::string& securityDir) {
    std::ifstream in(fs::path(securityDir) / "lockdown");
    if (!in) return "none";
    std::string content;
    std::getline(in, content);
    const auto open = content.find('[');
    const auto close = content.find(']');
    if (open == std::string::npos || close == std::string::npos || close <= open) return "none";
    return content.substr(open + 1, close - open - 1);
}

bool readSecureBoot(const std::string& efivarsDir) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(efivarsDir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("SecureBoot-", 0) != 0) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        char bytes[5] = {};
        in.read(bytes, sizeof bytes);
        return in.gcount() == 5 && bytes[4] == 1;  // 4-byte attr header + value
    }
    return false;  // no efivars / no variable => BIOS boot or SB unsupported
}

std::string readPrettyName(const std::string& osReleasePath) {
    std::ifstream in(osReleasePath);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        std::string value = line.substr(12);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}

LinuxSystemInfo::LinuxSystemInfo(Paths paths) : paths_(std::move(paths)) {}

core::Result<LinuxSystemInfo::Info> LinuxSystemInfo::query() {
    Info info;
    info.osVersion = readPrettyName(paths_.osRelease);
    utsname uts{};
    if (::uname(&uts) == 0) info.kernelVersion = uts.release;
    info.secureBoot = readSecureBoot(paths_.efivarsDir);
    info.lockdownMode = readLockdownMode(paths_.securityDir);
    info.rebootPending = false;  // Phase 6 owns update-driven reboot logic
    return info;
}

}  // namespace devmgr::platform_linux
```

- [x] **Step 4: Implement the kmod write side** — in `kmod_driver_manager.cpp`, replace the two Task 5 stubs (add `#include "devmgr/platform/linux/kmod_error_taxonomy.hpp"` and `#include "devmgr/platform/linux/linux_system_info.hpp"`):

```cpp
core::Result<void> KmodDriverManager::loadModule(const std::string& name) {
    auto mod = impl_->byName(name);
    if (!mod) return tl::unexpected(mod.error());

    // Pre-flight: refuse modprobe.d `install`-command modules — devmgrd never
    // fork/execs (spec §8.1; IGNORE_COMMAND below is belt-and-braces).
    const std::string wanted = normalized(name);
    if (kmod_config_iter* it = kmod_config_get_install_commands(impl_->ctx)) {
        std::string command;
        while (kmod_config_iter_next(it)) {
            const char* key = kmod_config_iter_get_key(it);
            const char* value = kmod_config_iter_get_value(it);
            if (key != nullptr && normalized(key) == wanted)
                command = value != nullptr ? value : "";
        }
        kmod_config_iter_free_iter(it);
        if (!command.empty()) {
            kmod_module_unref(*mod);
            return core::makeError(core::Error::Code::Unsupported,
                                   "module has a modprobe.d install rule (" + command +
                                       "); devmgrd does not execute shell commands — use modprobe");
        }
    }

    const int ret = kmod_module_probe_insert_module(
        *mod, KMOD_PROBE_APPLY_BLACKLIST | KMOD_PROBE_IGNORE_COMMAND, nullptr, nullptr, nullptr,
        nullptr);
    if (ret == 0) {
        kmod_module_unref(*mod);
        return {};  // loaded (or already loaded: idempotent success)
    }
    if (ret > 0) {  // stopped by blacklist (spec §8.1: distinct from failure)
        kmod_module_unref(*mod);
        return core::makeError(core::Error::Code::Unsupported, "blacklisted by modprobe.d");
    }
    // Negative errno: gather dependency initstates to name the culprit.
    std::vector<DepState> deps;
    kmod_list* deplist = kmod_module_get_dependencies(*mod);
    kmod_list* it = nullptr;
    kmod_list_foreach(it, deplist) {
        kmod_module* dep = kmod_module_get_module(it);
        const int state = kmod_module_get_initstate(dep);
        deps.push_back({kmod_module_get_name(dep),
                        state == KMOD_MODULE_LIVE || state == KMOD_MODULE_BUILTIN});
        kmod_module_unref(dep);
    }
    kmod_module_unref_list(deplist);
    kmod_module_unref(*mod);
    return tl::unexpected(describeLoadFailure(-ret, name, deps,
                                              readLockdownMode(impl_->options.securityDir)));
}

core::Result<void> KmodDriverManager::unloadModule(const std::string& name) {
    if (auto r = impl_->ready(); !r) return tl::unexpected(r.error());
    kmod_module* mod = nullptr;
    if (kmod_module_new_from_name(impl_->ctx, name.c_str(), &mod) < 0 || mod == nullptr)
        return core::makeError(core::Error::Code::NotFound, "module '" + name + "' not loaded");
    const int state = kmod_module_get_initstate(mod);
    if (state == KMOD_MODULE_BUILTIN) {
        kmod_module_unref(mod);
        return core::makeError(core::Error::Code::Unsupported,
                               "module '" + name + "' is built into the kernel");
    }
    if (state < 0) {
        kmod_module_unref(mod);
        return core::makeError(core::Error::Code::NotFound, "module '" + name + "' not loaded");
    }
    std::vector<std::string> holders;
    kmod_list* hlist = kmod_module_get_holders(mod);
    kmod_list* h = nullptr;
    kmod_list_foreach(h, hlist) {
        kmod_module* hm = kmod_module_get_module(h);
        holders.emplace_back(kmod_module_get_name(hm));
        kmod_module_unref(hm);
    }
    kmod_module_unref_list(hlist);
    const int ret = kmod_module_remove_module(mod, 0);
    kmod_module_unref(mod);
    if (ret == 0) return {};
    return tl::unexpected(describeUnloadFailure(-ret, name, holders));
}
```

(`describeLoadFailure`'s `<cerrno>`/`<system_error>` needs are satisfied inside the header via `<string>` + the existing includes — add `#include <cerrno>` and `#include <system_error>` to `kmod_error_taxonomy.hpp`.)

- [x] **Step 5: Run tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: taxonomy 4 + sysinfo 6 PASS; kmod write paths compile (their kernel-facing behavior is VM-tested, Task 13).

- [x] **Step 6: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(platform): Phase 5 T6 — kmod write side with install-rule refusal + load taxonomy, LinuxSystemInfo (Secure Boot, lockdown)"
```

---

### Task 7: RequestProcessor v2 — five verbs, persistent vs surgical, store integration

**Files:**
- Modify: `daemon/include/devmgr/daemon/request_processor.hpp`, `daemon/src/request_processor.cpp`
- Modify: `tests/unit/test_request_processor.cpp` (adapt ctor call sites + append)

**Interfaces:**
- Consumes: T1–T4 (interfaces, guard, controller semantics, StateStore), `services::makeDeviceKey/evaluateModuleUnload`.
- Produces (T8 shares the apply mutex; T9 adapts these EXACT signatures onto D-Bus):

```cpp
// request_processor.hpp (namespace devmgr::daemon) — full new shape
#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/state_store.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

inline constexpr const char* kActionSetDeviceEnabled = "org.devmgr.set-device-enabled";
inline constexpr const char* kActionManageModules = "org.devmgr.manage-modules";
inline constexpr const char* kActionManageDrivers = "org.devmgr.manage-drivers";

// validate → guard → authorize → act (unchanged, spec §6.2). New in Phase 5:
// SetDeviceEnabled persists desired state (StateStore); Bind/UnbindDriver are
// surgical and NEVER touch the store; module names are charset-validated; the
// apply mutex serializes every controller/store ACTION with EnforcementService.
class RequestProcessor {
   public:
    RequestProcessor(pal::IDeviceController& controller, pal::ICriticalityProber& prober,
                     IAuthority& authority, pal::IDriverManager& drivers,
                     pal::IDeviceEnumerator& enumerator, StateStore& store,
                     std::mutex& applyMutex, std::string sysfsRoot = "/sys");

    core::Result<void> setDeviceEnabled(const CallerId& caller, const std::string& sysfsPath,
                                        bool enabled);
    core::Result<void> loadModule(const CallerId& caller, const std::string& name);
    core::Result<void> unloadModule(const CallerId& caller, const std::string& name);
    core::Result<void> bindDriver(const CallerId& caller, const std::string& sysfsPath,
                                  const std::string& driverName);
    core::Result<void> unbindDriver(const CallerId& caller, const std::string& sysfsPath);
    std::vector<core::DisabledDeviceEntry> listDisabledDevices() const;  // read-only, no auth

   private:
    core::Result<std::string> canonicalContained(const std::string& sysfsPath) const;
    core::Result<void> authorize(const CallerId& caller, const char* action);

    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    IAuthority& authority_;
    pal::IDriverManager& drivers_;
    pal::IDeviceEnumerator& enumerator_;
    StateStore& store_;
    std::mutex& applyMutex_;
    std::string sysfsRoot_;
};

}  // namespace devmgr::daemon
```

- [x] **Step 1: Write the failing tests** — adapt the existing `test_request_processor.cpp` fixture to the new ctor (FakePal doubles as controller+drivers+enumerator; add a `StateStore` on a tmp dir and a `std::mutex`), then append. A tiny recording authority proves ordering:

```cpp
namespace {
class RecordingAuthority final : public devmgr::daemon::IAuthority {
   public:
    devmgr::core::Result<bool> checkAuthorized(const devmgr::daemon::CallerId&,
                                               const std::string& actionId) override {
        actions.push_back(actionId);
        return answer;
    }
    std::vector<std::string> actions;
    bool answer = true;
};
}  // namespace

TEST_F(RequestProcessorTest, LoadModuleValidatesNameBeforeAnything) {
    auto r = processor().loadModule(":1.7", "../evil");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "invalid module name");
    EXPECT_TRUE(authority_.actions.empty());   // never authorized
    EXPECT_TRUE(pal_.loadedModules.empty());   // never acted
}

TEST_F(RequestProcessorTest, LoadModuleUsesManageModulesActionThenActs) {
    ASSERT_TRUE(processor().loadModule(":1.7", "dummy").has_value());
    ASSERT_EQ(authority_.actions.size(), 1U);
    EXPECT_EQ(authority_.actions[0], "org.devmgr.manage-modules");
    ASSERT_EQ(pal_.loadedModules.size(), 1U);
    EXPECT_EQ(pal_.loadedModules[0], "dummy");
}

TEST_F(RequestProcessorTest, UnloadGuardRunsBeforeAuthorization) {
    devmgr::core::LoadedModule m;
    m.name = "usbcore";
    m.holders = {"usbhid"};
    pal_.seedLoadedModule(m);
    authority_.answer = false;  // would deny — but guard must refuse FIRST
    auto r = processor().unloadModule(":1.7", "usbcore");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::Conflict);
    EXPECT_TRUE(authority_.actions.empty());  // no polkit prompt on refusals
}

TEST_F(RequestProcessorTest, UnloadOfModuleBackingCriticalDeviceRefused) {
    devmgr::core::LoadedModule m;
    m.name = "nvme";
    pal_.seedLoadedModule(m);
    pal_.seedModuleDevices("nvme", {devicePath_});  // fixture device dir
    prober_.next = devmgr::pal::CriticalityFacts{.rootBackingPaths = {devicePath_ + "/nvme0"}};
    auto r = processor().unloadModule(":1.7", "nvme");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::Conflict);
    EXPECT_TRUE(pal_.unloadedModules.empty());
}

TEST_F(RequestProcessorTest, CleanUnloadSucceeds) {
    devmgr::core::LoadedModule m;
    m.name = "dummy";
    pal_.seedLoadedModule(m);
    ASSERT_TRUE(processor().unloadModule(":1.7", "dummy").has_value());
    ASSERT_EQ(pal_.unloadedModules.size(), 1U);
}

TEST_F(RequestProcessorTest, DisablePersistsEntryWithUnbindMechanismAndDriver) {
    pal_.unboundDriverResult = std::optional<std::string>{"virtio-pci"};
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, false).has_value());
    const auto entries = store_->entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].mechanism, "unbind");
    EXPECT_EQ(entries[0].lastDriver, "virtio-pci");
    EXPECT_EQ(entries[0].lastSysfsPath, devicePath_);
    EXPECT_GT(entries[0].disabledAtUtc, 0);
}

TEST_F(RequestProcessorTest, DisableWithAuthorizedMechanismPersistsToo) {
    pal_.unboundDriverResult = std::optional<std::string>{};  // nullopt => authorized
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, false).has_value());
    ASSERT_EQ(store_->entries().size(), 1U);
    EXPECT_EQ(store_->entries()[0].mechanism, "authorized");
}

TEST_F(RequestProcessorTest, EnableDeletesEntryThenRebindsWithStoredHint) {
    pal_.unboundDriverResult = std::optional<std::string>{"virtio-pci"};
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, false).has_value());
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, true).has_value());
    EXPECT_TRUE(store_->entries().empty());
    ASSERT_EQ(pal_.setEnabledCalls.size(), 2U);
    EXPECT_EQ(pal_.setEnabledCalls[1].hint, "virtio-pci");  // targeted rebind
}

TEST_F(RequestProcessorTest, SurgicalUnbindNeverTouchesTheStore) {
    ASSERT_TRUE(processor().unbindDriver(":1.7", devicePath_).has_value());
    EXPECT_TRUE(store_->entries().empty());          // spec §6.2: surgical
    ASSERT_EQ(authority_.actions.size(), 1U);
    EXPECT_EQ(authority_.actions[0], "org.devmgr.manage-drivers");
}

TEST_F(RequestProcessorTest, SurgicalUnbindStillGuarded) {
    prober_.next = devmgr::pal::CriticalityFacts{.rootBackingPaths = {devicePath_ + "/disk"}};
    auto r = processor().unbindDriver(":1.7", devicePath_);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, devmgr::core::Error::Code::Conflict);
}

TEST_F(RequestProcessorTest, BindDriverValidatesDriverName) {
    auto r = processor().bindDriver(":1.7", devicePath_, "evil/../driver");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().message, "invalid driver name");
}

TEST_F(RequestProcessorTest, ListDisabledDevicesExposesStoreEntries) {
    pal_.unboundDriverResult = std::optional<std::string>{};
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, false).has_value());
    EXPECT_EQ(processor().listDisabledDevices().size(), 1U);
}
```

Fixture requirements: `devicePath_` is a real tmp dir (canonical-containment passes), the FakePal seeds a Device whose `sysfsPath == devicePath_` (so the enumerator find succeeds), `store_` is a `std::unique_ptr<StateStore>` on a tmp dir with `load()` called, `processor()` builds `RequestProcessor(pal_, prober_, authority_, pal_, pal_, *store_, mutex_, root_)`.

- [x] **Step 2: Run to verify failure** — build fails (ctor/verbs missing).

- [x] **Step 3: Implement** — `daemon/src/request_processor.cpp` (full replacement):

```cpp
#include "devmgr/daemon/request_processor.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <utility>

#include "devmgr/services/critical_device_guard.hpp"
#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;

namespace {
bool validName(const std::string& name) {  // module / driver names: [A-Za-z0-9_-]+
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-';
    });
}
std::int64_t nowUtc() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
}  // namespace

RequestProcessor::RequestProcessor(pal::IDeviceController& controller,
                                   pal::ICriticalityProber& prober, IAuthority& authority,
                                   pal::IDriverManager& drivers,
                                   pal::IDeviceEnumerator& enumerator, StateStore& store,
                                   std::mutex& applyMutex, std::string sysfsRoot)
    : controller_(controller),
      prober_(prober),
      authority_(authority),
      drivers_(drivers),
      enumerator_(enumerator),
      store_(store),
      applyMutex_(applyMutex),
      sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<std::string> RequestProcessor::canonicalContained(
    const std::string& sysfsPath) const {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    const auto rel = canonical.lexically_relative(root);
    if (ec || rel.empty() || rel.native().starts_with(".."))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);
    return canonical.string();
}

core::Result<void> RequestProcessor::authorize(const CallerId& caller, const char* action) {
    auto authorized = authority_.checkAuthorized(caller, action);
    if (!authorized) return tl::unexpected(authorized.error());
    if (!*authorized) return core::makeError(core::Error::Code::Permission, "authorization denied");
    return {};
}

core::Result<void> RequestProcessor::setDeviceEnabled(const CallerId& caller,
                                                      const std::string& sysfsPath, bool enabled) {
    auto canonical = canonicalContained(sysfsPath);
    if (!canonical) return tl::unexpected(canonical.error());

    if (!enabled) {
        auto facts = prober_.probe();
        if (!facts) return tl::unexpected(facts.error());
        const auto verdict = services::evaluateDisable(*facts, *canonical);
        if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);
    }
    if (auto auth = authorize(caller, kActionSetDeviceEnabled); !auth) return auth;

    const std::lock_guard<std::mutex> lock(applyMutex_);
    if (!enabled) {
        // Key building needs the enumerated Device (vendor/product/serial +
        // cloned-serial downgrade against the present set, spec §5.1).
        auto all = enumerator_.enumerate();
        if (!all) return tl::unexpected(all.error());
        const auto device = std::find_if(all->begin(), all->end(), [&](const core::Device& d) {
            return d.sysfsPath == *canonical;
        });
        if (device == all->end())
            return core::makeError(core::Error::Code::NotFound,
                                   "device not enumerable: " + *canonical);
        auto applied = controller_.setEnabled(*canonical, false, "");
        if (!applied) return tl::unexpected(applied.error());
        core::DisabledDeviceEntry entry;
        entry.key = services::makeDeviceKey(*device, *all);
        entry.mechanism = applied->has_value() ? "unbind" : "authorized";
        entry.lastDriver = applied->has_value() ? **applied : "";
        entry.lastSysfsPath = *canonical;
        entry.disabledAtUtc = nowUtc();
        return store_.upsert(entry);
    }
    // Enable: delete the entry FIRST, then rebind — a rebind failure must
    // leave "enabled-but-unbound" with a clear error, never a lying store.
    std::string hint;
    for (const auto& e : store_.entries()) {
        if (e.lastSysfsPath == *canonical) {
            hint = e.lastDriver;
            auto removed = store_.remove(e.key);
            if (!removed) return removed;
            break;
        }
    }
    auto applied = controller_.setEnabled(*canonical, true, hint);
    if (!applied) return tl::unexpected(applied.error());
    return {};
}

core::Result<void> RequestProcessor::loadModule(const CallerId& caller, const std::string& name) {
    if (!validName(name))
        return core::makeError(core::Error::Code::NotFound, "invalid module name");
    if (auto auth = authorize(caller, kActionManageModules); !auth) return auth;
    const std::lock_guard<std::mutex> lock(applyMutex_);
    return drivers_.loadModule(name);
}

core::Result<void> RequestProcessor::unloadModule(const CallerId& caller,
                                                  const std::string& name) {
    if (!validName(name))
        return core::makeError(core::Error::Code::NotFound, "invalid module name");
    // Guard (spec §6.3): holders/refcount + criticality of bound devices.
    services::ModuleUnloadFacts moduleFacts;
    auto loaded = drivers_.listLoadedModules();
    if (!loaded) return tl::unexpected(loaded.error());
    const auto mod = std::find_if(loaded->begin(), loaded->end(),
                                  [&](const core::LoadedModule& m) { return m.name == name; });
    if (mod == loaded->end())
        return core::makeError(core::Error::Code::NotFound, "module '" + name + "' not loaded");
    moduleFacts.holders = mod->holders;
    moduleFacts.refCount = mod->refCount;
    auto affected = drivers_.devicesUsingModule(name);
    if (!affected) return tl::unexpected(affected.error());
    moduleFacts.affectedDevicePaths = *affected;
    auto facts = prober_.probe();
    if (!facts) return tl::unexpected(facts.error());
    const auto verdict = services::evaluateModuleUnload(*facts, moduleFacts);
    if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);

    if (auto auth = authorize(caller, kActionManageModules); !auth) return auth;
    const std::lock_guard<std::mutex> lock(applyMutex_);
    return drivers_.unloadModule(name);
}

core::Result<void> RequestProcessor::bindDriver(const CallerId& caller,
                                                const std::string& sysfsPath,
                                                const std::string& driverName) {
    auto canonical = canonicalContained(sysfsPath);
    if (!canonical) return tl::unexpected(canonical.error());
    if (!validName(driverName))
        return core::makeError(core::Error::Code::NotFound, "invalid driver name");
    if (auto auth = authorize(caller, kActionManageDrivers); !auth) return auth;
    const std::lock_guard<std::mutex> lock(applyMutex_);
    return controller_.bindDriver(*canonical, driverName);  // surgical: no store
}

core::Result<void> RequestProcessor::unbindDriver(const CallerId& caller,
                                                  const std::string& sysfsPath) {
    auto canonical = canonicalContained(sysfsPath);
    if (!canonical) return tl::unexpected(canonical.error());
    auto facts = prober_.probe();  // unbind ≡ disable risk (spec §6.1)
    if (!facts) return tl::unexpected(facts.error());
    const auto verdict = services::evaluateDisable(*facts, *canonical);
    if (!verdict.allowed) return core::makeError(core::Error::Code::Conflict, verdict.reason);
    if (auto auth = authorize(caller, kActionManageDrivers); !auth) return auth;
    const std::lock_guard<std::mutex> lock(applyMutex_);
    return controller_.unbindDriver(*canonical);  // surgical: no store
}

std::vector<core::DisabledDeviceEntry> RequestProcessor::listDisabledDevices() const {
    return store_.entries();
}

}  // namespace devmgr::daemon
```

- [x] **Step 4: Fix ripple + run tests**

`daemon/src/main.cpp` no longer compiles (ctor changed) — it is rewired properly in Task 9; for now pass the new deps minimally: construct `KmodDriverManager`, `UdevDeviceEnumerator`, `StateStore("/var/lib/devmgrd")` + `load()`, and a file-scope `std::mutex applyMutex;`. (Task 9 replaces this wiring with `--state-dir` and enforcement.)

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all PASS (Phase 4 processor tests updated + 12 new).

- [x] **Step 5: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(daemon): Phase 5 T7 — RequestProcessor v2: module load/unload with guard, surgical bind/unbind, persistent setDeviceEnabled via StateStore"
```

---

### Task 8: EnforcementService — startup sweep + hotplug re-apply

**Files:**
- Create: `daemon/include/devmgr/daemon/enforcement_service.hpp`, `daemon/src/enforcement_service.cpp`
- Modify: `daemon/CMakeLists.txt` (devmgrd_lib: add `src/enforcement_service.cpp`)
- Test: `tests/unit/test_enforcement_service.cpp` (+ register, `if(UNIX AND NOT APPLE)` block)

**Interfaces:**
- Consumes: StateStore, controller v2, prober, `services::matchesDevice`, `pal::HotplugEvent`.
- Produces (Task 9 wires `onHotplug` to the daemon's UdevHotplugMonitor):

```cpp
// enforcement_service.hpp (namespace devmgr::daemon)
#pragma once
#include <mutex>

#include "devmgr/daemon/state_store.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

// Active enforcement (spec §5.3): re-applies persisted desired state at
// startup (sweep) and on device reappearance (onHotplug). Guard is re-checked
// on EVERY re-apply — refusal marks the entry guardSuspended instead of
// enforcing. All failures log-and-continue; this class never throws.
class EnforcementService {
   public:
    EnforcementService(pal::IDeviceEnumerator& enumerator, pal::IDeviceController& controller,
                       pal::ICriticalityProber& prober, StateStore& store,
                       std::mutex& applyMutex);
    void sweep();
    void onHotplug(const pal::HotplugEvent& event);  // monitor callback thread

   private:
    void maybeReapply(const core::DisabledDeviceEntry& entry, const core::Device& device);

    pal::IDeviceEnumerator& enumerator_;
    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    StateStore& store_;
    std::mutex& applyMutex_;
};

}  // namespace devmgr::daemon
```

- [x] **Step 1: Write the failing tests** — create `tests/unit/test_enforcement_service.cpp`:

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <mutex>

#include "devmgr/daemon/enforcement_service.hpp"
#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_pal.hpp"

namespace fs = std::filesystem;
using devmgr::core::BusType;
using devmgr::core::Device;
using devmgr::core::DeviceKey;
using devmgr::core::DeviceStatus;
using devmgr::core::DisabledDeviceEntry;
using devmgr::daemon::EnforcementService;
using devmgr::daemon::StateStore;

class EnforcementServiceTest : public ::testing::Test {
   protected:
    fs::path dir_;
    devmgr::test::FakePal pal_;
    devmgr::test::FakeCriticalityProber prober_;
    std::mutex mutex_;
    std::unique_ptr<StateStore> store_;

    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("devmgr-enforce-" + std::string(::testing::UnitTest::GetInstance()
                                                    ->current_test_info()
                                                    ->name()));
        fs::remove_all(dir_);
        store_ = std::make_unique<StateStore>(dir_.string());
        ASSERT_TRUE(store_->load().has_value());
    }
    void TearDown() override { fs::remove_all(dir_); }

    EnforcementService service() {
        return EnforcementService(pal_, pal_, prober_, *store_, mutex_);
    }

    static Device usbDevice(const std::string& path, const std::string& serial) {
        Device d;
        d.bus = BusType::Usb;
        d.sysfsPath = path;
        d.vendorId = "046d";
        d.productId = "c52b";
        d.serial = serial;
        d.status = DeviceStatus::Active;  // kernel re-enabled it (flicker window)
        return d;
    }
    static DisabledDeviceEntry entryFor(const Device& d) {
        return DisabledDeviceEntry{.key = devmgr::services::makeDeviceKey(d),
                                   .mechanism = "authorized",
                                   .lastDriver = "",
                                   .lastSysfsPath = d.sysfsPath,
                                   .disabledAtUtc = 1,
                                   .guardSuspended = false};
    }
};

TEST_F(EnforcementServiceTest, SweepReappliesDisableToReenabledDevice) {
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);
    ASSERT_TRUE(store_->upsert(entryFor(d)).has_value());
    service().sweep();
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1U);
    EXPECT_EQ(pal_.setEnabledCalls[0].sysfsPath, d.sysfsPath);
    EXPECT_FALSE(pal_.setEnabledCalls[0].enabled);
}

TEST_F(EnforcementServiceTest, SweepSkipsAlreadyDisabledDevice) {
    auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    d.status = DeviceStatus::Disabled;
    pal_.seedDevice(d);
    ASSERT_TRUE(store_->upsert(entryFor(d)).has_value());
    service().sweep();
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}

TEST_F(EnforcementServiceTest, HotplugReappearanceAtNewPortReappliesAndUpdatesPath) {
    const auto original = usbDevice("/sys/devices/usb2/2-1", "AB12");
    ASSERT_TRUE(store_->upsert(entryFor(original)).has_value());
    const auto moved = usbDevice("/sys/devices/usb1/1-9", "AB12");  // serial tier matches
    auto svc = service();
    svc.onHotplug({devmgr::pal::HotplugEvent::Action::Added, moved});
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1U);
    EXPECT_EQ(pal_.setEnabledCalls[0].sysfsPath, moved.sysfsPath);
    EXPECT_EQ(store_->entries()[0].lastSysfsPath, moved.sysfsPath);
}

TEST_F(EnforcementServiceTest, GuardRefusalSuspendsInsteadOfEnforcing) {
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);
    ASSERT_TRUE(store_->upsert(entryFor(d)).has_value());
    // Topology changed: this device now hosts the root disk.
    prober_.next = devmgr::pal::CriticalityFacts{.rootBackingPaths = {d.sysfsPath + "/disk"}};
    service().sweep();
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
    EXPECT_TRUE(store_->entries()[0].guardSuspended);
}

TEST_F(EnforcementServiceTest, SuccessfulReapplyClearsSuspension) {
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);
    auto e = entryFor(d);
    e.guardSuspended = true;
    ASSERT_TRUE(store_->upsert(e).has_value());
    service().sweep();
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1U);
    EXPECT_FALSE(store_->entries()[0].guardSuspended);
}

TEST_F(EnforcementServiceTest, ControllerFailureIsLoggedAndDoesNotThrow) {
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    // NOT seeded into pal_ devices' enabled_ map => setEnabled returns NotFound.
    ASSERT_TRUE(store_->upsert(entryFor(d)).has_value());
    auto svc = service();
    EXPECT_NO_THROW(svc.onHotplug({devmgr::pal::HotplugEvent::Action::Added, d}));
    EXPECT_EQ(store_->entries().size(), 1U);  // entry stays for the next try
}

TEST_F(EnforcementServiceTest, RemovalEventsAreIgnored) {
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    ASSERT_TRUE(store_->upsert(entryFor(d)).has_value());
    auto svc = service();
    svc.onHotplug({devmgr::pal::HotplugEvent::Action::Removed, d});
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}
```

(Add `#include "devmgr/services/device_key.hpp"` to the test.)

- [x] **Step 2: Run to verify failure** — header missing.

- [x] **Step 3: Implement** — `daemon/src/enforcement_service.cpp`:

```cpp
#include "devmgr/daemon/enforcement_service.hpp"

#include <spdlog/spdlog.h>

#include "devmgr/services/critical_device_guard.hpp"
#include "devmgr/services/device_key.hpp"

namespace devmgr::daemon {

EnforcementService::EnforcementService(pal::IDeviceEnumerator& enumerator,
                                       pal::IDeviceController& controller,
                                       pal::ICriticalityProber& prober, StateStore& store,
                                       std::mutex& applyMutex)
    : enumerator_(enumerator),
      controller_(controller),
      prober_(prober),
      store_(store),
      applyMutex_(applyMutex) {}

void EnforcementService::sweep() {
    auto devices = enumerator_.enumerate();
    if (!devices) {
        spdlog::warn("enforcement sweep: enumeration failed: {}", devices.error().message);
        return;
    }
    for (const auto& entry : store_.entries()) {
        for (const auto& device : *devices) {
            if (services::matchesDevice(entry.key, device) ||
                entry.lastSysfsPath == device.sysfsPath) {
                maybeReapply(entry, device);
                break;
            }
        }
    }
}

void EnforcementService::onHotplug(const pal::HotplugEvent& event) {
    if (event.action == pal::HotplugEvent::Action::Removed) return;
    const auto entry = store_.findFor(event.device);
    if (!entry) return;
    maybeReapply(*entry, event.device);
}

void EnforcementService::maybeReapply(const core::DisabledDeviceEntry& entry,
                                      const core::Device& device) {
    // Already in the desired state? authorized mechanism shows as Disabled in
    // the mapper; unbind mechanism shows as no bound driver.
    const bool needsApply = entry.mechanism == "authorized"
                                ? device.status != core::DeviceStatus::Disabled
                                : device.boundDriver.has_value();
    if (!needsApply) return;

    // Guard re-check on EVERY re-apply (spec §5.3): topology may have changed.
    auto facts = prober_.probe();
    if (!facts) {
        spdlog::warn("enforcement: prober failed for {}: {}", device.sysfsPath,
                     facts.error().message);
        return;
    }
    const auto verdict = services::evaluateDisable(*facts, device.sysfsPath);
    if (!verdict.allowed) {
        spdlog::warn("enforcement suspended for {}: {}", device.sysfsPath, verdict.reason);
        if (auto r = store_.setGuardSuspended(entry.key, true); !r)
            spdlog::warn("enforcement: cannot persist suspension: {}", r.error().message);
        return;
    }

    const std::lock_guard<std::mutex> lock(applyMutex_);
    auto applied = controller_.setEnabled(device.sysfsPath, false, "");
    if (!applied) {  // log-and-continue: never crash the daemon over one device
        spdlog::warn("enforcement: re-apply failed for {}: {}", device.sysfsPath,
                     applied.error().message);
        return;
    }
    spdlog::info("enforcement: re-disabled {}", device.sysfsPath);
    if (entry.lastSysfsPath != device.sysfsPath) {
        if (auto r = store_.setLastSysfsPath(entry.key, device.sysfsPath); !r)
            spdlog::warn("enforcement: cannot update path: {}", r.error().message);
    }
    if (entry.guardSuspended) {
        if (auto r = store_.setGuardSuspended(entry.key, false); !r)
            spdlog::warn("enforcement: cannot clear suspension: {}", r.error().message);
    }
}

}  // namespace devmgr::daemon
```

(devmgrd_lib already links spdlog transitively via devmgr_core; if the link fails, add `spdlog::spdlog` PRIVATE.)

- [x] **Step 4: Run tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R Enforcement --output-on-failure`
Expected: 7 PASS; full suite green.

- [x] **Step 5: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(daemon): Phase 5 T8 — EnforcementService: startup sweep, hotplug re-apply, guard suspension, log-and-continue"
```

---

### Task 9: IPC v2 — contract, adaptor, channel, polkit, daemon wiring, round-trip tests

**Files:**
- Modify: `platform/linux/include/devmgr/platform/linux/dbus_contract.hpp` (`kApiVersion = 2`)
- Modify: `daemon/src/request_processor.cpp` + `tests/unit/test_request_processor.cpp` (enumerator-fallback, Step 1)
- Modify: `daemon/src/manager_adaptor.cpp`
- Modify: `daemon/src/main.cpp`, `daemon/data/org.devmgr.policy`
- Modify: `platform/linux/include/devmgr/platform/linux/dbus_privileged_channel.hpp`, `platform/linux/src/dbus_privileged_channel.cpp`
- Test: `tests/ipc/test_ipc_round_trip.cpp` (append)

**Interfaces:**
- Consumes: T7 verbs, T8 service, T5/T6 KmodDriverManager, `UdevHotplugMonitor`/`UdevDeviceEnumerator` (existing).
- Produces: `org.devmgr.Manager1` v2 (spec §6.1 table) and the channel methods T10's facade calls. Wire schema for `ListDisabledDevices` → `aa{sv}` with keys: `bus`, `vendor_id`, `product_id`, `serial`, `position`, `mechanism`, `last_driver`, `last_sysfs_path` (strings), `disabled_at_utc` (int64), `guard_suspended` (bool).

- [x] **Step 1: Enumerator fallback in setDeviceEnabled (with test)**

The private-bus ipc tests (and any device udev misses) disable devices under a fake `--sysfs-root`, which the real `UdevDeviceEnumerator` never lists. Relax T7's "device not enumerable → NotFound": when the enumerated set lacks the device, build the key from sysfs attributes directly. In `request_processor.cpp` replace the `device == all->end()` error with:

```cpp
        core::DisabledDeviceEntry entry;
        if (device != all->end()) {
            entry.key = services::makeDeviceKey(*device, *all);
        } else {
            entry.key = services::makeDeviceKey(deviceFromSysfs(*canonical));
        }
```

(also tolerate `enumerate()` failure by treating it as an empty set), with this file-local helper in the anonymous namespace:

```cpp
core::Device deviceFromSysfs(const std::string& canonical) {
    const fs::path dir(canonical);
    auto attr = [&](const char* name) -> std::string {
        std::ifstream in(dir / name);
        std::string v;
        std::getline(in, v);
        if (v.rfind("0x", 0) == 0) v = v.substr(2);
        return v;
    };
    core::Device d;
    d.sysfsPath = canonical;
    std::error_code ec;
    const std::string bus = fs::weakly_canonical(dir / "subsystem", ec).filename().string();
    d.bus = bus == "usb"     ? core::BusType::Usb
            : bus == "pci"   ? core::BusType::Pci
            : bus == "platform" ? core::BusType::Platform
            : bus == "virtio"   ? core::BusType::Virtio
                                : core::BusType::Other;
    const std::string vendor = attr("idVendor");
    d.vendorId = vendor.empty() ? attr("vendor") : vendor;
    const std::string product = attr("idProduct");
    d.productId = product.empty() ? attr("device") : product;
    d.serial = attr("serial");
    return d;
}
```

(add `#include <fstream>`). Unit test appended to `test_request_processor.cpp`:

```cpp
namespace {
struct EmptyEnumerator final : devmgr::pal::IDeviceEnumerator {
    devmgr::core::Result<std::vector<devmgr::core::Device>> enumerate() override {
        return std::vector<devmgr::core::Device>{};
    }
};
}  // namespace

TEST_F(RequestProcessorTest, DisableOfUnenumeratedDeviceFallsBackToSysfsKey) {
    // Device dir exists on disk (validation passes, FakePal-as-controller is
    // seeded) but the enumerator sees NOTHING — the key must come from attrs.
    // makeDeviceDir(rel, attrs) = fixture helper: creates root_/devices/<rel>
    // with the given attr files + a `subsystem` symlink to root_/bus/usb.
    const std::string ghost = makeDeviceDir("usb9/9-1", {{"idVendor", "0x1234"},
                                                         {"idProduct", "0x5678"},
                                                         {"serial", "GH0ST"}});
    devmgr::core::Device seeded;
    seeded.sysfsPath = ghost;
    pal_.seedDevice(seeded);  // controller side only
    EmptyEnumerator empty;
    devmgr::daemon::RequestProcessor processor(pal_, prober_, authority_, pal_, empty, *store_,
                                               mutex_, root_.string());
    auto r = processor.setDeviceEnabled(":1.7", ghost, false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    const auto entries = store_->entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].key.bus, "usb");        // from the subsystem link
    EXPECT_EQ(entries[0].key.vendorId, "1234");  // 0x stripped
    EXPECT_EQ(entries[0].key.serial, "GH0ST");   // from the serial attr
}
```

Run: processor tests all green before moving on.

- [x] **Step 2: Contract + policy**

`dbus_contract.hpp`: `inline constexpr std::uint32_t kApiVersion = 2;` — nothing else changes (error names cover the new verbs).
`daemon/data/org.devmgr.policy` — add before `</policyconfig>`:

```xml
  <action id="org.devmgr.manage-modules">
    <description>Load or unload a kernel module</description>
    <message>Authentication is required to load or unload a kernel module</message>
    <defaults>
      <allow_any>auth_admin</allow_any>
      <allow_inactive>auth_admin</allow_inactive>
      <allow_active>auth_admin_keep</allow_active>
    </defaults>
  </action>
  <action id="org.devmgr.manage-drivers">
    <description>Bind or unbind a device driver</description>
    <message>Authentication is required to bind or unbind a device driver</message>
    <defaults>
      <allow_any>auth_admin</allow_any>
      <allow_inactive>auth_admin</allow_inactive>
      <allow_active>auth_admin_keep</allow_active>
    </defaults>
  </action>
```

- [x] **Step 3: ManagerAdaptor v2** — replace the vTable registration in `manager_adaptor.cpp`:

```cpp
#include "daemon/src/manager_adaptor.hpp"

#include <map>
#include <string>
#include <vector>

#include "devmgr/platform/linux/dbus_contract.hpp"

namespace devmgr::daemon {
namespace {
void throwIfFailed(const core::Result<void>& result) {
    if (!result)
        throw sdbus::Error(
            sdbus::Error::Name{platform_linux::dbusErrorNameFor(result.error().code)},
            result.error().message);
}
}  // namespace

ManagerAdaptor::ManagerAdaptor(sdbus::IConnection& connection, RequestProcessor& processor)
    : processor_(processor) {
    object_ = sdbus::createObject(connection, sdbus::ObjectPath{platform_linux::kObjectPath});
    auto sender = [this] {
        return std::string{object_->getCurrentlyProcessedMessage().getSender()};
    };
    object_
        ->addVTable(
            sdbus::registerMethod("SetDeviceEnabled")
                .withInputParamNames("sysfs_path", "enabled")
                .implementedAs([this, sender](const std::string& path, const bool enabled) {
                    throwIfFailed(processor_.setDeviceEnabled(sender(), path, enabled));
                }),
            sdbus::registerMethod("LoadModule")
                .withInputParamNames("name")
                .implementedAs([this, sender](const std::string& name) {
                    throwIfFailed(processor_.loadModule(sender(), name));
                }),
            sdbus::registerMethod("UnloadModule")
                .withInputParamNames("name")
                .implementedAs([this, sender](const std::string& name) {
                    throwIfFailed(processor_.unloadModule(sender(), name));
                }),
            sdbus::registerMethod("BindDriver")
                .withInputParamNames("sysfs_path", "driver")
                .implementedAs([this, sender](const std::string& path, const std::string& driver) {
                    throwIfFailed(processor_.bindDriver(sender(), path, driver));
                }),
            sdbus::registerMethod("UnbindDriver")
                .withInputParamNames("sysfs_path")
                .implementedAs([this, sender](const std::string& path) {
                    throwIfFailed(processor_.unbindDriver(sender(), path));
                }),
            sdbus::registerMethod("ListDisabledDevices")
                .withOutputParamNames("entries")
                .implementedAs([this] {
                    std::vector<std::map<std::string, sdbus::Variant>> out;
                    for (const auto& e : processor_.listDisabledDevices()) {
                        out.push_back({{"bus", sdbus::Variant(e.key.bus)},
                                       {"vendor_id", sdbus::Variant(e.key.vendorId)},
                                       {"product_id", sdbus::Variant(e.key.productId)},
                                       {"serial", sdbus::Variant(e.key.serial)},
                                       {"position", sdbus::Variant(e.key.position)},
                                       {"mechanism", sdbus::Variant(e.mechanism)},
                                       {"last_driver", sdbus::Variant(e.lastDriver)},
                                       {"last_sysfs_path", sdbus::Variant(e.lastSysfsPath)},
                                       {"disabled_at_utc", sdbus::Variant(e.disabledAtUtc)},
                                       {"guard_suspended", sdbus::Variant(e.guardSuspended)}});
                    }
                    return out;
                }),
            sdbus::registerProperty("ApiVersion").withGetter([] {
                return platform_linux::kApiVersion;
            }))
        .forInterface(sdbus::InterfaceName{platform_linux::kInterfaceName});
}

}  // namespace devmgr::daemon
```

- [x] **Step 4: DbusPrivilegedChannel v2** — header becomes:

```cpp
#pragma once
#include <cstdint>
#include <mutex>
#include <optional>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// System-bus client of devmgrd (spec §6.1). Every call opens a fresh
// connection (Phase 4 pattern). v2 verbs check ApiVersion >= 2 once and cache
// it; Phase 4 SetDeviceEnabled keeps working against an old daemon.
class DbusPrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    enum class Bus { System, Session };
    explicit DbusPrivilegedChannel(Bus bus = Bus::System);

    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override;
    core::Result<void> loadModule(const std::string& name) override;
    core::Result<void> unloadModule(const std::string& name) override;
    core::Result<void> bindDriver(const core::Device& device,
                                  const std::string& driverName) override;
    core::Result<void> unbindDriver(const core::Device& device) override;
    core::Result<std::vector<core::DisabledDeviceEntry>> listDisabledDevices() override;

   private:
    core::Result<void> ensureApi2();
    Bus bus_;
    std::mutex cacheMutex_;
    std::optional<std::uint32_t> cachedApi_;
};

}  // namespace devmgr::platform_linux
```

`.cpp`: keep `setDeviceEnabled` as-is; add a file-local proxy factory and the new methods:

```cpp
namespace {
std::unique_ptr<sdbus::IProxy> makeProxy(DbusPrivilegedChannel::Bus bus) {
    auto connection = bus == DbusPrivilegedChannel::Bus::Session
                          ? sdbus::createSessionBusConnection()
                          : sdbus::createSystemBusConnection();
    return sdbus::createProxy(std::move(connection), sdbus::ServiceName{kBusName},
                              sdbus::ObjectPath{kObjectPath});
}
}  // namespace

core::Result<void> DbusPrivilegedChannel::ensureApi2() {
    {
        const std::lock_guard<std::mutex> lock(cacheMutex_);
        if (cachedApi_) {
            if (*cachedApi_ >= 2) return {};
            return core::makeError(core::Error::Code::Unsupported,
                                   "devmgrd too old (API " + std::to_string(*cachedApi_) +
                                       " < 2) — restart the daemon");
        }
    }
    try {
        auto proxy = makeProxy(bus_);
        const sdbus::Variant v =
            proxy->getProperty("ApiVersion").onInterface(sdbus::InterfaceName{kInterfaceName});
        const auto api = v.get<std::uint32_t>();
        const std::lock_guard<std::mutex> lock(cacheMutex_);
        cachedApi_ = api;
        if (api >= 2) return {};
        return core::makeError(core::Error::Code::Unsupported,
                               "devmgrd too old (API " + std::to_string(api) +
                                   " < 2) — restart the daemon");
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}
```

Each v2 verb follows one shape (shown once; repeat with its method name/args):

```cpp
core::Result<void> DbusPrivilegedChannel::loadModule(const std::string& name) {
    if (auto api = ensureApi2(); !api) return api;
    try {
        auto proxy = makeProxy(bus_);
        proxy->callMethod("LoadModule")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(name)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}
// unloadModule => "UnloadModule", (name)
// bindDriver   => "BindDriver",   (device.sysfsPath, driverName)
// unbindDriver => "UnbindDriver", (device.sysfsPath)
```

`listDisabledDevices` deserializes the map array (uses a short 5-second timeout — it is a read on the refresh path, never an interactive-auth wait):

```cpp
core::Result<std::vector<core::DisabledDeviceEntry>> DbusPrivilegedChannel::listDisabledDevices() {
    if (auto api = ensureApi2(); !api) return tl::unexpected(api.error());
    try {
        auto proxy = makeProxy(bus_);
        std::vector<std::map<std::string, sdbus::Variant>> raw;
        proxy->callMethod("ListDisabledDevices")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withTimeout(std::chrono::seconds(5))
            .storeResultsTo(raw);
        std::vector<core::DisabledDeviceEntry> out;
        for (const auto& m : raw) {
            core::DisabledDeviceEntry e;
            e.key.bus = m.at("bus").get<std::string>();
            e.key.vendorId = m.at("vendor_id").get<std::string>();
            e.key.productId = m.at("product_id").get<std::string>();
            e.key.serial = m.at("serial").get<std::string>();
            e.key.position = m.at("position").get<std::string>();
            e.mechanism = m.at("mechanism").get<std::string>();
            e.lastDriver = m.at("last_driver").get<std::string>();
            e.lastSysfsPath = m.at("last_sysfs_path").get<std::string>();
            e.disabledAtUtc = m.at("disabled_at_utc").get<std::int64_t>();
            e.guardSuspended = m.at("guard_suspended").get<bool>();
            out.push_back(std::move(e));
        }
        return out;
    } catch (const sdbus::Error& e) {
        return tl::unexpected(coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}
```

- [x] **Step 5: Daemon wiring** — `daemon/src/main.cpp`: add `--state-dir` (default `/var/lib/devmgrd`) to the flag table + usage line; non-default triggers the test-mode warning. Composition (replacing the Task 7 interim wiring):

```cpp
        platform_linux::SysfsDeviceController controller(opts.sysfsRoot);
        platform_linux::LinuxCriticalityProber prober(opts.sysfsRoot, opts.mountsPath);
        platform_linux::KmodDriverManager drivers({.sysfsRoot = opts.sysfsRoot});
        platform_linux::UdevDeviceEnumerator enumerator;
        daemon::StateStore store(opts.stateDir);
        if (auto loaded = store.load(); !loaded)
            spdlog::warn("state store load failed: {}", loaded.error().message);
        std::mutex applyMutex;
        auto authority = makeAuthority(opts.authority);
        daemon::RequestProcessor processor(controller, prober, *authority, drivers, enumerator,
                                           store, applyMutex, opts.sysfsRoot);
        daemon::EnforcementService enforcement(enumerator, controller, prober, store, applyMutex);
        enforcement.sweep();  // startup re-apply (spec §5.3)
        platform_linux::UdevHotplugMonitor monitor;
        if (auto started = monitor.start(
                [&enforcement](const pal::HotplugEvent& e) { enforcement.onHotplug(e); });
            !started)
            spdlog::warn("hotplug watch unavailable ({}): enforcement is sweep-only",
                         started.error().message);
        daemon::ManagerAdaptor adaptor(*connection, processor);
        spdlog::info("devmgrd serving {} (api {}) on the {} bus", platform_linux::kBusName,
                     platform_linux::kApiVersion, opts.bus);
        connection->enterEventLoop();
        monitor.stop();
```

Add the includes: `kmod_driver_manager.hpp`, `udev_device_enumerator.hpp`, `udev_hotplug_monitor.hpp`, `state_store.hpp`, `enforcement_service.hpp`, `<mutex>`.

- [x] **Step 6: Round-trip tests** — extend `tests/ipc/test_ipc_round_trip.cpp`. Fixture changes: `startDaemon` gains the state dir: `::execl(DEVMGRD_BIN, "devmgrd", "--bus", "session", "--sysfs-root", root_.c_str(), "--mounts-path", (root_/"mounts").c_str(), "--state-dir", (root_/"state").c_str(), "--authority", authority, nullptr)`. Add a `stopDaemon()` helper (the TearDown kill/wait body, callable mid-test). Add USB identity attrs + subsystem to SetUp's device (`idVendor` `0x1234`, `idProduct` `0x5678`, `serial` `IPCSER`, symlink `subsystem` → `root_/bus/usb` after `fs::create_directories(root_/"bus/usb")`). New tests:

```cpp
TEST_F(IpcRoundTripTest, DisabledDeviceAppearsInBulkListAndClearsOnEnable) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), false).has_value());
    auto list = channel.listDisabledDevices();
    ASSERT_TRUE(list.has_value()) << list.error().message;
    ASSERT_EQ(list->size(), 1U);
    EXPECT_EQ((*list)[0].mechanism, "authorized");
    EXPECT_EQ((*list)[0].key.serial, "IPCSER");
    ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), true).has_value());
    auto after = channel.listDisabledDevices();
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->empty());
}

TEST_F(IpcRoundTripTest, DesiredStateSurvivesDaemonRestartAndSweepReapplies) {
    startDaemon("allow-all");
    {
        DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
        ASSERT_TRUE(channel.setDeviceEnabled(deviceAt(device_.string()), false).has_value());
    }
    stopDaemon();
    std::ofstream(device_ / "authorized") << "1";  // "replug": kernel re-enables
    startDaemon("allow-all");                       // sweep must re-disable
    for (int i = 0; i < 100 && readFile(device_ / "authorized") != "0"; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
}

TEST_F(IpcRoundTripTest, SurgicalUnbindGoesThroughTheBusAndSkipsTheStore) {
    // PCI device bound to a driver in the fake tree (Task 3 layout).
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:03.0";
    const fs::path drv = root_ / "bus/pci/drivers/virtio-pci";
    fs::create_directories(pci);
    fs::create_directories(drv);
    std::ofstream(drv / "unbind") << "";
    std::ofstream(drv / "bind") << "";
    std::ofstream(root_ / "bus/pci/drivers_probe") << "";
    fs::create_directory_symlink(drv, pci / "driver");
    fs::create_directory_symlink(root_ / "bus/pci", pci / "subsystem");
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    ASSERT_TRUE(channel.unbindDriver(deviceAt(pci.string())).has_value());
    EXPECT_EQ(readFile(drv / "unbind"), "0000:00:03.0");
    auto list = channel.listDisabledDevices();
    ASSERT_TRUE(list.has_value());
    EXPECT_TRUE(list->empty());  // surgical: never persisted
    ASSERT_TRUE(channel.bindDriver(deviceAt(pci.string()), "virtio-pci").has_value());
    EXPECT_EQ(readFile(drv / "bind"), "0000:00:03.0");
}

TEST_F(IpcRoundTripTest, InvalidModuleNameRefusedAsNotFound) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.loadModule("../evil");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(r.error().message, "invalid module name");
}
```

NOTE: `GuardRefusalArrivesAsConflictWithReason` (Phase 4) still passes — its device now also matches the enumerator fallback path harmlessly.

- [x] **Step 7: Run everything**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: unit suite + `devmgr_ipc` all PASS (ipc runs under dbus-run-session; requires sdbus-c++ present — on the host this is portage 2.3.1; otherwise defer to the container run).

- [x] **Step 8: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(ipc): Phase 5 T9 — org.devmgr.Manager1 ApiVersion 2: module/driver verbs, bulk ListDisabledDevices, enforcement wiring, polkit actions"
```

---

### Task 10: App layer — facade v2, disabled-state overlay, ModulesVM, detail driver section

**Files:**
- Create: `app/include/devmgr/app/disabled_overlay.hpp`, `app/src/disabled_overlay.cpp`
- Create: `app/include/devmgr/app/modules_vm.hpp`, `app/src/modules_vm.cpp`
- Modify: `app/include/devmgr/app/application_facade.hpp`, `app/src/application_facade.cpp`
- Modify: `app/src/device_detail_vm.cpp`
- Modify: `app/CMakeLists.txt` (add the two new .cpp)
- Test: `tests/unit/test_disabled_overlay.cpp`, `tests/unit/test_modules_vm.cpp` (+ register), `tests/unit/test_application_facade.cpp` (append), `tests/unit/test_device_detail_vm.cpp` (append)

**Interfaces:**
- Consumes: T1 channel/driver interfaces + fakes, `services::matchesDevice`, existing `TaskScheduler`/`EventBus`/`IUiDispatcher`/`DeviceService`.
- Produces (T11/T12 wire these EXACT signatures):

```cpp
// disabled_overlay.hpp (namespace devmgr::app)
#pragma once
#include <vector>
#include "devmgr/core/models.hpp"
namespace devmgr::app {
// Daemon-owned truth merge (spec §6.1/§9.1): a device matching a desired-
// disabled entry renders Disabled — including during the replug flicker while
// it is transiently bound. guardSuspended surfaces via errorNote.
void applyDisabledOverlay(std::vector<core::Device>& devices,
                          const std::vector<core::DisabledDeviceEntry>& entries);
}  // namespace devmgr::app
```

`ApplicationFacade` — ctor gains two optional seams after `prober`:
`pal::IDriverManager* drivers = nullptr, pal::ISystemInfo* systemInfo = nullptr`
(members `drivers_`, `systemInfo_`). New public methods:

```cpp
    // Reads (sync; {} / advisory-allowed degradation when seams are null).
    std::vector<core::Driver> driverInfo(const core::DeviceId& id) const;
    core::Result<std::vector<core::LoadedModule>> listModules() const;
    core::Result<core::Driver> moduleDetail(const std::string& name) const;
    core::Result<core::ModprobeInfo> modprobeDetail(const std::string& name) const;
    std::optional<pal::ISystemInfo::Info> systemInfo() const;
    services::GuardVerdict canUnloadModule(const std::string& name) const;
    // Mutations (async, Phase 4 pattern): ONE TaskCompletedEvent each, taskId
    // prefixes "load-module:", "unload-module:", "bind-driver:", "unbind-driver:".
    // Module mutations ALSO publish ModulesChangedEvent on success.
    std::future<void> loadModule(const std::string& name);
    std::future<void> unloadModule(const std::string& name);
    std::future<void> bindDriver(const core::DeviceId& id, const std::string& driverName);
    std::future<void> unbindDriver(const core::DeviceId& id);
```

```cpp
// modules_vm.hpp (namespace devmgr::app) — DeviceListVM's seam pattern
#pragma once
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"

namespace devmgr::app {

// Toolkit-agnostic Modules view model: filtered rows over listModules(), a
// selected module, an async-filled signature column (spec §7.1 perf note),
// and a Secure Boot/lockdown banner. Subscribes ModulesChangedEvent and
// rebuilds via the dispatcher (UI thread only, like DeviceListVM).
class ModulesVM {
   public:
    ModulesVM(ApplicationFacade& facade, runtime::EventBus& bus,
              runtime::TaskScheduler& scheduler, IUiDispatcher& dispatcher);
    ~ModulesVM();  // waits on the in-flight signature fill (future custody)

    std::vector<std::string>& rowsRef() { return rows_; }
    int& selectedRef() { return selected_; }
    void setFilter(std::string filter);
    std::optional<std::string> selectedModule() const;
    std::vector<std::string> detailLines() const;  // selected module deep info
    std::string banner() const;
    void setRebuildHooks(std::function<void()> before, std::function<void()> after);
    void rebuild();                            // UI thread: snapshot + rows
    // Async: fills the signature cache for names not yet cached, then posts a
    // rebuild. Returns a waitable handle (tests); the dtor also waits on it.
    std::shared_future<void> fillSignatures();

   private:
    void onModulesChanged();  // EventBus handler → coalesced dispatcher post

    ApplicationFacade& facade_;
    runtime::EventBus& bus_;
    runtime::TaskScheduler& scheduler_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    std::vector<std::string> rows_;
    std::vector<std::optional<std::string>> rowNames_;  // nullopt == placeholder row
    std::vector<core::LoadedModule> snapshot_;
    std::map<std::string, std::string> signatureCell_;  // name -> "yes (…)" | "NO" | "…"
    std::atomic<bool> rebuildQueued_{false};
    int selected_ = 0;
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
    std::shared_future<void> sigFill_;
    runtime::Subscription subModules_;
};

}  // namespace devmgr::app
```

Row format (fixed columns, shared by both UIs):
`printf("%-28s %9s %4ld  %-24s %s", name, sizeKb, refCount, holdersJoined, signatureCell)` where `sizeKb` = `std::to_string(sizeBytes / 1024) + "K"`, holders joined with `,` truncated to 24 chars, signature cell defaults to `"…"` until the fill completes.

- [x] **Step 1: Write the failing tests**

`tests/unit/test_disabled_overlay.cpp`:

```cpp
#include <gtest/gtest.h>

#include "devmgr/app/disabled_overlay.hpp"
#include "devmgr/services/device_key.hpp"

using devmgr::core::BusType;
using devmgr::core::Device;
using devmgr::core::DeviceStatus;
using devmgr::core::DisabledDeviceEntry;

namespace {
Device usb(std::string path, std::string serial) {
    Device d;
    d.bus = BusType::Usb;
    d.sysfsPath = std::move(path);
    d.vendorId = "046d";
    d.productId = "c52b";
    d.serial = std::move(serial);
    d.status = DeviceStatus::Active;
    return d;
}
}  // namespace

TEST(DisabledOverlay, MatchingDeviceRendersDisabledEvenWhileTransientlyBound) {
    std::vector<Device> devices{usb("/sys/devices/usb1/1-9", "AB12")};  // replugged, new port
    DisabledDeviceEntry e;
    e.key = devmgr::services::makeDeviceKey(usb("/sys/devices/usb2/2-1", "AB12"));
    e.lastSysfsPath = "/sys/devices/usb2/2-1";
    devmgr::app::applyDisabledOverlay(devices, {e});
    EXPECT_EQ(devices[0].status, DeviceStatus::Disabled);  // flicker suppressed
}

TEST(DisabledOverlay, GuardSuspensionSurfacesInErrorNote) {
    std::vector<Device> devices{usb("/sys/devices/usb2/2-1", "AB12")};
    DisabledDeviceEntry e;
    e.key = devmgr::services::makeDeviceKey(devices[0]);
    e.guardSuspended = true;
    devmgr::app::applyDisabledOverlay(devices, {e});
    ASSERT_TRUE(devices[0].errorNote.has_value());
    EXPECT_NE(devices[0].errorNote->find("enforcement suspended"), std::string::npos);
}

TEST(DisabledOverlay, UnrelatedDevicesUntouched) {
    std::vector<Device> devices{usb("/sys/devices/usb2/2-2", "OTHER")};
    DisabledDeviceEntry e;
    e.key = devmgr::services::makeDeviceKey(usb("/sys/devices/usb2/2-1", "AB12"));
    devmgr::app::applyDisabledOverlay(devices, {e});
    EXPECT_EQ(devices[0].status, DeviceStatus::Active);
}
```

`tests/unit/test_modules_vm.cpp` (uses `fakes/fake_pal.hpp`, `fakes/inline_ui_dispatcher.hpp`):

```cpp
#include <gtest/gtest.h>

#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using devmgr::app::ApplicationFacade;
using devmgr::app::ModulesVM;

class ModulesVMTest : public ::testing::Test {
   protected:
    devmgr::runtime::EventBus bus_;
    devmgr::runtime::TaskScheduler scheduler_;
    devmgr::test::FakePal pal_;
    devmgr::app::DeviceService service_{bus_};
    devmgr::test::InlineUiDispatcher dispatcher_;
    ApplicationFacade facade_{pal_, scheduler_, bus_, service_, nullptr, nullptr, &pal_, &pal_};
    // ModulesVM holds references + a Subscription: construct in place per test
    // (it is neither copyable nor movable).

    void seed(const std::string& name, long refs) {
        devmgr::core::LoadedModule m;
        m.name = name;
        m.sizeBytes = 4096;
        m.refCount = refs;
        pal_.seedLoadedModule(m);
    }
};

TEST_F(ModulesVMTest, RebuildListsModulesWithPlaceholderSignature) {
    seed("dummy", 0);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_NE(v.rowsRef()[0].find("dummy"), std::string::npos);
    EXPECT_NE(v.rowsRef()[0].find("…"), std::string::npos);  // async fill pending
    EXPECT_EQ(v.selectedModule(), "dummy");
}

TEST_F(ModulesVMTest, FilterNarrowsRows) {
    seed("dummy", 0);
    seed("usbhid", 2);
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.setFilter("usb");
    ASSERT_EQ(v.rowsRef().size(), 1U);
    EXPECT_NE(v.rowsRef()[0].find("usbhid"), std::string::npos);
}

TEST_F(ModulesVMTest, SignatureFillReplacesPlaceholder) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.isSigned = true;
    info.signer = "Build key";
    pal_.seedDriver("/anywhere", info);  // moduleInfo() finds by name
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    v.fillSignatures().wait();
    v.rebuild();  // the ModulesChangedEvent normally triggers this via dispatcher
    EXPECT_NE(v.rowsRef()[0].find("yes (Build key)"), std::string::npos);
}

TEST_F(ModulesVMTest, BannerReportsSecureBootAndLockdown) {
    pal_.info.secureBoot = true;
    pal_.info.lockdownMode = "integrity";
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    EXPECT_EQ(v.banner(),
              "Secure Boot: ON · Lockdown: integrity — unsigned modules will be rejected");
    pal_.info.secureBoot = false;
    pal_.info.lockdownMode = "none";
    EXPECT_EQ(v.banner(), "Secure Boot: off · Lockdown: none");
}

TEST_F(ModulesVMTest, DetailLinesIncludeModprobeInfo) {
    seed("dummy", 0);
    devmgr::core::Driver info;
    info.name = "dummy";
    info.version = "1.0";
    info.path = "/lib/modules/x/dummy.ko";
    pal_.seedDriver("/anywhere", info);
    pal_.modprobeResult = devmgr::core::ModprobeInfo{.options = "numdummies=2",
                                                     .blacklisted = true};
    ModulesVM v(facade_, bus_, scheduler_, dispatcher_);
    v.rebuild();
    const auto lines = v.detailLines();
    const std::string all = [&] {
        std::string s;
        for (const auto& l : lines) s += l + "\n";
        return s;
    }();
    EXPECT_NE(all.find("numdummies=2"), std::string::npos);
    EXPECT_NE(all.find("blacklisted"), std::string::npos);
}
```

Append to `tests/unit/test_application_facade.cpp` (fixture already has bus/scheduler/service/FakePal; add a `FakePrivilegedChannel channel_;` where missing and construct facades with `&pal_` as drivers/systemInfo):

```cpp
TEST_F(ApplicationFacadeTest, LoadModulePublishesCompletionAndModulesChanged) {
    int modulesChanged = 0;
    auto sub = bus_.subscribe<devmgr::core::ModulesChangedEvent>(
        [&](const devmgr::core::ModulesChangedEvent&) { ++modulesChanged; });
    std::optional<devmgr::core::TaskCompletedEvent> done;
    auto sub2 = bus_.subscribe<devmgr::core::TaskCompletedEvent>(
        [&](const devmgr::core::TaskCompletedEvent& e) { done = e; });
    facadeWithChannel().loadModule("dummy").wait();
    ASSERT_TRUE(done.has_value());
    EXPECT_EQ(done->taskId, "load-module:dummy");
    EXPECT_TRUE(done->ok);
    EXPECT_EQ(modulesChanged, 1);
    ASSERT_EQ(channel_.moduleCalls.size(), 1U);
    EXPECT_EQ(channel_.moduleCalls[0], "load:dummy");
}

TEST_F(ApplicationFacadeTest, UnbindDriverResolvesDeviceAndCallsChannel) {
    seedOneDevice();  // fixture helper: a device with known id/path
    facadeWithChannel().unbindDriver(knownId()).wait();
    ASSERT_EQ(channel_.moduleCalls.size(), 1U);
    EXPECT_EQ(channel_.moduleCalls[0], "unbind:" + knownPath());
}

TEST_F(ApplicationFacadeTest, RefreshMergesDisabledOverlayFromChannel) {
    seedOneDevice();
    devmgr::core::DisabledDeviceEntry e;
    e.lastSysfsPath = knownPath();
    channel_.disabledEntries = std::vector<devmgr::core::DisabledDeviceEntry>{e};
    facadeWithChannel().refresh().wait();
    const auto devices = facadeWithChannel().devices();
    ASSERT_FALSE(devices.empty());
    EXPECT_EQ(devices[0].status, devmgr::core::DeviceStatus::Disabled);
}

TEST_F(ApplicationFacadeTest, CanUnloadModuleAdvisesInUse) {
    devmgr::core::LoadedModule m;
    m.name = "usbcore";
    m.holders = {"usbhid"};
    pal_.seedLoadedModule(m);
    const auto verdict = facadeWithChannel().canUnloadModule("usbcore");
    EXPECT_FALSE(verdict.allowed);
    EXPECT_EQ(verdict.reason, "in use by usbhid");
}
```

(`facadeWithChannel()`, `seedOneDevice()`, `knownId()`, `knownPath()` — adapt to the fixture's existing helpers; construct the facade as `{pal_, scheduler_, bus_, service_, &channel_, nullptr, &pal_, &pal_}`.)

Append to `tests/unit/test_device_detail_vm.cpp`:

```cpp
TEST_F(DeviceDetailVMTest, DriverSectionListsBoundFirstWithSignature) {
    // Fixture device with boundDriver "usbhid"; seed candidates:
    devmgr::core::Driver bound;
    bound.name = "usbhid";
    bound.isSigned = true;
    bound.signer = "Build key";
    devmgr::core::Driver candidate;
    candidate.name = "dummy";
    pal_.seedDriver(devicePath(), bound);
    pal_.seedDriver(devicePath(), candidate);
    const auto lines = vm().lines(knownId());
    const std::string all = joined(lines);
    EXPECT_NE(all.find("— Driver —"), std::string::npos);
    EXPECT_NE(all.find("* usbhid"), std::string::npos);   // bound marker
    EXPECT_NE(all.find("signed: Build key"), std::string::npos);
    EXPECT_NE(all.find("  dummy"), std::string::npos);    // candidate, unmarked
}
```

- [x] **Step 2: Run to verify failure** — build fails on the new headers/methods.

- [x] **Step 3: Implement `disabled_overlay.cpp`**

```cpp
#include "devmgr/app/disabled_overlay.hpp"

#include "devmgr/services/device_key.hpp"

namespace devmgr::app {

void applyDisabledOverlay(std::vector<core::Device>& devices,
                          const std::vector<core::DisabledDeviceEntry>& entries) {
    for (auto& device : devices) {
        for (const auto& entry : entries) {
            if (services::matchesDevice(entry.key, device) ||
                entry.lastSysfsPath == device.sysfsPath) {
                device.status = core::DeviceStatus::Disabled;
                if (entry.guardSuspended)
                    device.errorNote = "disabled — enforcement suspended (guard refused re-apply)";
                break;
            }
        }
    }
}

}  // namespace devmgr::app
```

- [x] **Step 4: Facade v2** — in `application_facade.cpp`:

`refresh()` becomes:

```cpp
std::future<void> ApplicationFacade::refresh() {
    return scheduler_.submit([this] {
        auto result = enumerator_.enumerate();
        if (!result) {
            bus_.publish(
                core::ErrorEvent{.source = "enumerate", .message = result.error().message});
            return;
        }
        if (channel_ != nullptr) {
            // ONE bulk fetch per refresh (spec §6.1); daemon-down or API-1
            // degrades silently to Phase 4 rendering.
            if (auto disabled = channel_->listDisabledDevices(); disabled)
                applyDisabledOverlay(*result, *disabled);
        }
        service_.applyEnumeration(std::move(*result));
    });
}
```

New reads:

```cpp
std::vector<core::Driver> ApplicationFacade::driverInfo(const core::DeviceId& id) const {
    if (drivers_ == nullptr) return {};
    const auto device = service_.findById(id);
    if (!device) return {};
    auto result = drivers_->driversFor(*device);
    return result ? *result : std::vector<core::Driver>{};
}

core::Result<std::vector<core::LoadedModule>> ApplicationFacade::listModules() const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->listLoadedModules();
}

core::Result<core::Driver> ApplicationFacade::moduleDetail(const std::string& name) const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->moduleInfo(name);
}

core::Result<core::ModprobeInfo> ApplicationFacade::modprobeDetail(const std::string& name) const {
    if (drivers_ == nullptr)
        return core::makeError(core::Error::Code::Unsupported, "built without kmod support");
    return drivers_->modprobeInfo(name);
}

std::optional<pal::ISystemInfo::Info> ApplicationFacade::systemInfo() const {
    if (systemInfo_ == nullptr) return std::nullopt;
    auto info = systemInfo_->query();
    if (!info) return std::nullopt;
    return *info;
}

services::GuardVerdict ApplicationFacade::canUnloadModule(const std::string& name) const {
    if (drivers_ == nullptr || prober_ == nullptr) return {};  // advisory unavailable
    services::ModuleUnloadFacts moduleFacts;
    auto loaded = drivers_->listLoadedModules();
    if (!loaded) return {};
    for (const auto& m : *loaded) {
        if (m.name != name) continue;
        moduleFacts.holders = m.holders;
        moduleFacts.refCount = m.refCount;
    }
    if (auto affected = drivers_->devicesUsingModule(name); affected)
        moduleFacts.affectedDevicePaths = *affected;
    auto facts = prober_->probe();
    if (!facts) return {};
    return services::evaluateModuleUnload(*facts, moduleFacts);
}
```

Mutations — one private helper keeps them DRY (add to the header's private section: `std::future<void> runChannelTask(std::string taskId, std::string okMessage, bool modulesChanged, std::function<core::Result<void>(pal::IPrivilegedChannel&)> call);`):

```cpp
std::future<void> ApplicationFacade::runChannelTask(
    std::string taskId, std::string okMessage, bool modulesChanged,
    std::function<core::Result<void>(pal::IPrivilegedChannel&)> call) {
    return scheduler_.submit([this, taskId = std::move(taskId),
                              okMessage = std::move(okMessage), modulesChanged,
                              call = std::move(call)] {
        if (channel_ == nullptr) {
            bus_.publish(
                core::TaskCompletedEvent{.taskId = taskId,
                                         .ok = false,
                                         .message = "built without privileged-helper support"});
            return;
        }
        auto result = call(*channel_);
        if (result) {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = true, .message = okMessage});
            if (modulesChanged) bus_.publish(core::ModulesChangedEvent{});
        } else {
            bus_.publish(core::TaskCompletedEvent{
                .taskId = taskId, .ok = false, .message = result.error().message});
        }
    });
}

std::future<void> ApplicationFacade::loadModule(const std::string& name) {
    return runChannelTask("load-module:" + name, "Loaded module " + name, true,
                          [name](pal::IPrivilegedChannel& c) { return c.loadModule(name); });
}

std::future<void> ApplicationFacade::unloadModule(const std::string& name) {
    return runChannelTask("unload-module:" + name, "Unloaded module " + name, true,
                          [name](pal::IPrivilegedChannel& c) { return c.unloadModule(name); });
}

std::future<void> ApplicationFacade::bindDriver(const core::DeviceId& id,
                                                const std::string& driverName) {
    const auto device = service_.findById(id);
    if (!device) {
        return runChannelTask("bind-driver:" + id.value, "", false,
                              [](pal::IPrivilegedChannel&) -> core::Result<void> {
                                  return core::makeError(core::Error::Code::NotFound,
                                                         "device no longer present");
                              });
    }
    return runChannelTask("bind-driver:" + id.value,
                          "Bound " + driverName + " to " + device->name, false,
                          [device = *device, driverName](pal::IPrivilegedChannel& c) {
                              return c.bindDriver(device, driverName);
                          });
}

std::future<void> ApplicationFacade::unbindDriver(const core::DeviceId& id) {
    const auto device = service_.findById(id);
    if (!device) {
        return runChannelTask("unbind-driver:" + id.value, "", false,
                              [](pal::IPrivilegedChannel&) -> core::Result<void> {
                                  return core::makeError(core::Error::Code::NotFound,
                                                         "device no longer present");
                              });
    }
    return runChannelTask("unbind-driver:" + id.value, "Unbound driver from " + device->name,
                          false, [device = *device](pal::IPrivilegedChannel& c) {
                              return c.unbindDriver(device);
                          });
}
```

(Add includes: `disabled_overlay.hpp`; `setDeviceEnabled` and `canDisable` stay unchanged.)

- [x] **Step 5: Implement `modules_vm.cpp`**

```cpp
#include "devmgr/app/modules_vm.hpp"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {

ModulesVM::ModulesVM(ApplicationFacade& facade, runtime::EventBus& bus,
                     runtime::TaskScheduler& scheduler, IUiDispatcher& dispatcher)
    : facade_(facade), bus_(bus), scheduler_(scheduler), dispatcher_(dispatcher) {
    subModules_ = bus_.subscribe<core::ModulesChangedEvent>(
        [this](const core::ModulesChangedEvent&) { onModulesChanged(); });
}

ModulesVM::~ModulesVM() {
    if (sigFill_.valid()) sigFill_.wait();  // no publish into a dead VM
}

void ModulesVM::onModulesChanged() {
    if (rebuildQueued_.exchange(true)) return;  // coalesce bursts
    dispatcher_.post([this] {
        rebuildQueued_.store(false);
        rebuild();
    });
}

void ModulesVM::setFilter(std::string filter) {
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    filter_ = std::move(filter);
    rebuild();
}

void ModulesVM::setRebuildHooks(std::function<void()> before, std::function<void()> after) {
    beforeRebuild_ = std::move(before);
    afterRebuild_ = std::move(after);
}

void ModulesVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    const auto keep = selectedModule();
    auto loaded = facade_.listModules();
    snapshot_ = loaded ? std::move(*loaded) : std::vector<core::LoadedModule>{};
    std::sort(snapshot_.begin(), snapshot_.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    rows_.clear();
    rowNames_.clear();
    for (const auto& m : snapshot_) {
        std::string haystack = m.name;
        std::transform(haystack.begin(), haystack.end(), haystack.begin(), ::tolower);
        if (!filter_.empty() && haystack.find(filter_) == std::string::npos) continue;
        std::string holders;
        for (const auto& h : m.holders) {
            if (!holders.empty()) holders += ",";
            holders += h;
        }
        if (holders.size() > 24) holders = holders.substr(0, 21) + "…";
        const auto sig = signatureCell_.find(m.name);
        char row[128];
        std::snprintf(row, sizeof row, "%-28s %8lluK %4ld  %-24s %s", m.name.c_str(),
                      static_cast<unsigned long long>(m.sizeBytes / 1024), m.refCount,
                      holders.c_str(), sig != signatureCell_.end() ? sig->second.c_str() : "…");
        rows_.emplace_back(row);
        rowNames_.emplace_back(m.name);
    }
    if (rows_.empty()) {
        rows_.emplace_back(filter_.empty() ? "(no modules)" : "(no matches)");
        rowNames_.emplace_back(std::nullopt);
    }
    // Restore selection by name, then clamp.
    if (keep) {
        for (int i = 0; i < static_cast<int>(rowNames_.size()); ++i)
            if (rowNames_[i] == keep) selected_ = i;
    }
    selected_ = std::clamp(selected_, 0, static_cast<int>(rows_.size()) - 1);
    if (afterRebuild_) afterRebuild_();
}

std::optional<std::string> ModulesVM::selectedModule() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(rowNames_.size())) return std::nullopt;
    return rowNames_[selected_];
}

std::shared_future<void> ModulesVM::fillSignatures() {
    // Snapshot the UNCACHED names on the caller (UI) thread (spec §7.1 perf:
    // re-entering the view never re-reads .ko files already classified); the
    // worker only touches the facade + a local map, then posts the merge back.
    std::vector<std::string> names;
    for (const auto& m : snapshot_)
        if (signatureCell_.find(m.name) == signatureCell_.end()) names.push_back(m.name);
    if (names.empty()) return sigFill_;  // possibly invalid: nothing to wait on
    sigFill_ = scheduler_
                   .submit([this, names = std::move(names)] {
                       std::map<std::string, std::string> cells;
                       for (const auto& name : names) {
                           auto info = facade_.moduleDetail(name);
                           if (!info) {
                               cells[name] = "?";
                           } else if (info->isSigned) {
                               cells[name] =
                                   "yes" + (info->signer ? " (" + *info->signer + ")" : "");
                           } else {
                               cells[name] = "NO";
                           }
                       }
                       dispatcher_.post([this, cells = std::move(cells)]() mutable {
                           for (auto& [k, v] : cells) signatureCell_[k] = std::move(v);
                           rebuild();
                       });
                   })
                   .share();
    return sigFill_;
}

std::string ModulesVM::banner() const {
    const auto info = facade_.systemInfo();
    if (!info) return "Secure Boot: unknown";
    std::string b = std::string("Secure Boot: ") + (info->secureBoot ? "ON" : "off") +
                    " · Lockdown: " + info->lockdownMode;
    if (info->secureBoot || info->lockdownMode != "none")
        b += " — unsigned modules will be rejected";
    return b;
}

std::vector<std::string> ModulesVM::detailLines() const {
    const auto name = selectedModule();
    if (!name) return {"(no module selected)"};
    std::vector<std::string> out;
    out.push_back("Module:  " + *name);
    if (auto info = facade_.moduleDetail(*name); info) {
        if (!info->version.empty()) out.push_back("Version: " + info->version);
        if (!info->path.empty()) out.push_back("Path:    " + info->path);
        out.push_back(std::string("Signed:  ") +
                      (info->isSigned ? "yes" + (info->signer ? " (" + *info->signer + ")" : "")
                                      : "NO"));
        if (!info->dependencies.empty()) {
            std::string deps;
            for (const auto& d : info->dependencies) {
                if (!deps.empty()) deps += ", ";
                deps += d;
            }
            out.push_back("Depends: " + deps);
        }
    }
    if (auto mp = facade_.modprobeDetail(*name); mp) {
        if (mp->options) out.push_back("Options: " + *mp->options);
        if (mp->blacklisted) out.push_back("modprobe.d: blacklisted");
    }
    return out;
}

}  // namespace devmgr::app
```

NOTE on `fillSignatures()`: the `.share()` form is deliberate — one worker does the reads, every caller (tests, dtor) can wait on the same handle, and it is safe regardless of `TaskScheduler`'s pool size.

- [x] **Step 6: DeviceDetailVM driver section** — append in `lines(...)` before `return out;`:

```cpp
    const auto drivers = facade_.driverInfo(d.id);
    if (!drivers.empty()) {
        out.push_back("— Driver —");
        for (const auto& drv : drivers) {
            const bool bound = d.boundDriver.has_value() && drv.name == *d.boundDriver;
            std::string line = (bound ? "* " : "  ") + drv.name;
            if (drv.kind == core::DriverKind::Builtin) line += " (builtin)";
            if (!drv.version.empty()) line += " v" + drv.version;
            if (drv.kind != core::DriverKind::Builtin)
                line += drv.isSigned
                            ? " — signed: " + drv.signer.value_or("unknown signer")
                            : " — UNSIGNED";
            out.push_back(line);
            if (!drv.dependencies.empty()) {
                std::string deps;
                for (const auto& dep : drv.dependencies) {
                    if (!deps.empty()) deps += ", ";
                    deps += dep;
                }
                out.push_back("    depends: " + deps);
            }
        }
    }
```

(Adjust the T10 detail test's expected substrings to exactly these formats: `"* usbhid"`, `"— signed: Build key"`.)

- [x] **Step 7: Run tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all PASS (3 overlay + 5 ModulesVM + 4 facade + 1 detail new).

- [x] **Step 8: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(app): Phase 5 T10 — facade v2 (module/driver ops, disabled overlay merge), ModulesVM with async signature fill, detail driver section"
```

---

### Task 11: TUI — Modules screen + driver actions

**Files:**
- Modify: `tui/src/tui_app.cpp`

**Interfaces:**
- Consumes: T10 facade/ModulesVM, T5/T6 `KmodDriverManager`/`LinuxSystemInfo`.
- Produces: keys `m` (Devices ⇄ Modules), Modules: `l` load / `u` unload, Devices detail: `U` unbind / `B` bind. All prompts render on the status line (Phase 4 confirm pattern). TUI has no unit-test target — verification is the offscreen regression + manual smoke.

- [x] **Step 1: Composition root** — in `runTuiApp()` after `prober`:

```cpp
    platform_linux::KmodDriverManager kmod;   // system defaults: /sys, real modules
    platform_linux::LinuxSystemInfo sysinfo;
```

and change the two facade constructions to append `, &kmod, &sysinfo`:

```cpp
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, &channel, &prober, &kmod,
                                  &sysinfo);
    // (and the #else branch with nullptr for the channel)
```

After `statusVm`: `app::ModulesVM modulesVm(facade, bus, scheduler, dispatcher);`

- [x] **Step 2: Modules layout + tab switch** — after the existing `layout`:

```cpp
    auto modulesMenu = Menu(&modulesVm.rowsRef(), &modulesVm.selectedRef(), MenuOption::Vertical());
    std::string bannerText;  // computed on tab entry — banner() reads sysfs, never per frame
    std::string moduleFilter;
    InputOption modFilterOpt;
    modFilterOpt.content = &moduleFilter;
    modFilterOpt.placeholder = "filter modules…";
    modFilterOpt.on_change = [&] { modulesVm.setFilter(moduleFilter); };
    auto moduleFilterInput = Input(modFilterOpt);

    auto moduleDetail = Renderer([&] {
        Elements els;
        for (const auto& line : modulesVm.detailLines()) els.push_back(text(line));
        return vbox(std::move(els)) | flex;
    });
    auto modulesPane = Container::Vertical({moduleFilterInput, modulesMenu});
    auto modulesLayout = Container::Horizontal({modulesPane, moduleDetail});

    int activeTab = 0;  // 0 = devices, 1 = modules
    auto tabs = Container::Tab({layout, modulesLayout}, &activeTab);
```

Replace `auto ui = Renderer(layout, [&] {` with `auto ui = Renderer(tabs, [&] {` and make the render body pick per tab — the devices body stays byte-identical; the modules body:

```cpp
        if (activeTab == 1) {
            return vbox({
                       text(" Modules (m=devices  l=load  u=unload  q=quit) ") | bold,
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
```

where `statusLine()` is a small lambda consolidating the pending-prompt-or-status text (see Step 3).

- [x] **Step 3: Modal states + keys** — replace the single `confirmToggle` block with a superset. Above the renderers:

```cpp
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
```

(The existing devices-tab render body's status-line expression changes from `confirmToggle ? confirmToggle->prompt : statusVm.text()` to `statusLine()`.) The `CatchEvent` handler becomes:

```cpp
    auto root = CatchEvent(ui, [&](const Event& event) {
        if (event == Event::Custom) {
            detailDirty = true;
            dispatcher.drain();
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
        if (event == Event::Character('m')) {
            activeTab = activeTab == 0 ? 1 : 0;
            if (activeTab == 1) {
                bannerText = modulesVm.banner();
                modulesVm.rebuild();
                modulesVm.fillSignatures();  // cached names are skipped
            }
            return true;
        }
        if (event == Event::Character('q')) {
            screen.Exit();
            return true;
        }
        if (activeTab == 1) {  // ----- modules keys -----
            if (event == Event::Character('l')) {
                textPrompt = PendingText{
                    .onSubmit =
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
                    bus.publish(core::TaskCompletedEvent{.taskId = "guard",
                                                         .ok = false,
                                                         .message = "cannot unload: " +
                                                                    verdict.reason});
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
        // ----- devices keys (activeTab == 0) -----
        if (event == Event::Character('e')) {
            /* UNCHANGED Phase 4 body, but arm `confirm` instead of confirmToggle:
               confirm = PendingConfirm{.onYes = [&, id = *id, enable] {
                   prunePending();
                   pending.push_back(facade.setDeviceEnabled(id, enable)); },
                   .prompt = ...same prompt...}; */
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
            confirm = PendingConfirm{
                .onYes =
                    [&, id = *id] {
                        prunePending();
                        pending.push_back(facade.unbindDriver(id));
                    },
                .prompt = "unbind driver from " + device->name + "? (advanced, not persistent) (y/n)"};
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
            textPrompt = PendingText{
                .onSubmit =
                    [&, id = *id](const std::string& driver) {
                        prunePending();
                        pending.push_back(facade.bindDriver(id, driver));
                    },
                .prompt = "bind driver to " + device->name + ": ",
                .buffer = prefill};
            return true;
        }
        if (event == Event::Character('r')) {
            prunePending();
            pending.push_back(facade.refresh());
            return true;
        }
        if (event == Event::Escape) {
            screen.Exit();
            return true;
        }
        return false;
    });
```

Notes: delete the old `PendingToggle` struct; update the devices header line to `" Devices (r=refresh  e=enable/disable  U=unbind  B=bind  m=modules  q=quit) "`; `modulesVm` is declared alongside the other VMs so the existing teardown (`hotplug.stop(); delayed.shutdown(); drainPending(pending);`) already precedes its destruction — its dtor then waits on any in-flight signature fill. Requires `#include <cctype>`, `devmgr/app/modules_vm.hpp`, `devmgr/platform/linux/kmod_driver_manager.hpp`, `devmgr/platform/linux/linux_system_info.hpp`.

- [x] **Step 4: Build + offscreen regression**

Run: `cmake --build --preset linux-debug`
Run: `printf 'q' | timeout 10 ./build/linux-debug/tui/devmgr-tui; echo "exit=$?"`
Expected: `exit=0` (the Phase 4 quit regression still holds).
Run: `printf 'mq' | timeout 10 ./build/linux-debug/tui/devmgr-tui; echo "exit=$?"`
Expected: `exit=0` (modules screen opens, signature fill starts, teardown clean).

- [x] **Step 5: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(tui): Phase 5 T11 — Modules screen (banner/list/detail/load/unload) + device unbind/bind keys"
```

---

### Task 12: GUI — Modules tab + driver actions (offscreen-tested)

**Files:**
- Create: `gui/src/module_list_model.hpp`, `gui/src/module_list_model.cpp`
- Modify: `gui/src/main_window.hpp`, `gui/src/main_window.cpp`, `gui/src/gui_app.cpp`, `gui/CMakeLists.txt`
- Test: `gui/tests/test_module_list_model.cpp` (+ register in `gui/CMakeLists.txt` test target), `gui/tests/test_main_window.cpp` (append)

**Interfaces:**
- Consumes: T10 ModulesVM/facade; existing `QtUiDispatcher`, `DeviceListModel` patterns.
- Produces: `MainWindow` grows a QTabWidget central (Devices | Modules) and an `Actions` callback struct so the composition root keeps owning future custody (Phase 3/4 pattern).

- [x] **Step 1: ModuleListModel**

`gui/src/module_list_model.hpp`:

```cpp
#pragma once
#include <QAbstractListModel>

#include "devmgr/app/modules_vm.hpp"

namespace devmgr::gui {

// Qt mirror of ModulesVM's row list (DeviceListModel pattern: the VM's
// rebuild hooks drive begin/endResetModel; rows are preformatted strings).
class ModuleListModel final : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit ModuleListModel(app::ModulesVM& vm, QObject* parent = nullptr);
    ~ModuleListModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;

   private:
    app::ModulesVM& vm_;
};

}  // namespace devmgr::gui
```

`gui/src/module_list_model.cpp`:

```cpp
#include "gui/src/module_list_model.hpp"

namespace devmgr::gui {

ModuleListModel::ModuleListModel(app::ModulesVM& vm, QObject* parent)
    : QAbstractListModel(parent), vm_(vm) {
    vm_.setRebuildHooks([this] { beginResetModel(); }, [this] { endResetModel(); });
    vm_.rebuild();
}

ModuleListModel::~ModuleListModel() {
    vm_.setRebuildHooks({}, {});
}

int ModuleListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(vm_.rowsRef().size());
}

QVariant ModuleListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount({})) return {};
    if (role != Qt::DisplayRole) return {};
    return QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
}

}  // namespace devmgr::gui
```

- [x] **Step 2: MainWindow — Actions struct + Modules tab**

`main_window.hpp` — replace the two `std::function` ctor params (`onRefresh`, `onSetEnabled`) and `confirm` with one struct (update members accordingly; existing `confirm_` behavior folds in):

```cpp
    struct Actions {
        std::function<void()> onRefresh;
        std::function<void(const core::DeviceId&, bool)> onSetEnabled;
        std::function<void(const std::string&)> onLoadModule;
        std::function<void(const std::string&)> onUnloadModule;
        std::function<void(const core::DeviceId&, const std::string&)> onBindDriver;
        std::function<void(const core::DeviceId&)> onUnbindDriver;
        std::function<bool(const QString&)> confirm;          // {} => QMessageBox
        std::function<QString(const QString&, const QString&)> textInput;  // {} => QInputDialog
    };
    MainWindow(app::ApplicationFacade& facade, app::DeviceListVM& listVm,
               app::DeviceDetailVM& detailVm, app::StatusLineVM& statusVm,
               app::ModulesVM& modulesVm, QtUiDispatcher& dispatcher, Actions actions,
               QWidget* parent = nullptr);
```

New test accessors: `QTabWidget* tabs() const`, `QListView* modulesView() const`, `QAction* loadModuleAction() const`, `QAction* unloadModuleAction() const`, `QAction* unbindAction() const`, `QAction* bindAction() const`, `QLabel* bannerLabel() const`. New members: `modulesVm_`, `actions_`, `tabs_`, `modulesView_`, `moduleModel_`, `moduleFilterEdit_`, `bannerLabel_`, the four QActions, `moduleDetailTree_`.

`main_window.cpp` — construction additions (after the existing toggleAction_ block):

```cpp
    loadModuleAction_ = toolbar->addAction(QStringLiteral("Load Module…"));
    connect(loadModuleAction_, &QAction::triggered, this, [this] {
        const QString name =
            actions_.textInput
                ? actions_.textInput(QStringLiteral("Load module"), QString{})
                : QInputDialog::getText(this, QStringLiteral("Load module"),
                                        QStringLiteral("Module name:"));
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!name.isEmpty() && valid.match(name).hasMatch())
            actions_.onLoadModule(name.toStdString());
    });

    unloadModuleAction_ = toolbar->addAction(QStringLiteral("Unload"));
    connect(unloadModuleAction_, &QAction::triggered, this, [this] {
        const auto name = modulesVm_.selectedModule();
        if (!name) return;
        const auto verdict = facade_.canUnloadModule(*name);
        if (!verdict.allowed) {
            statusBar()->showMessage(
                QString::fromStdString("cannot unload: " + verdict.reason));
            return;
        }
        const QString prompt = QStringLiteral("Unload module %1?")
                                   .arg(QString::fromStdString(*name));
        if (askConfirm(prompt)) actions_.onUnloadModule(*name);
    });

    unbindAction_ = toolbar->addAction(QStringLiteral("Unbind driver (advanced)"));
    connect(unbindAction_, &QAction::triggered, this, [this] {
        const auto id = listVm_.selectedDeviceId();
        const auto device = id ? facade_.findById(*id) : std::nullopt;
        if (!device) return;
        const auto verdict = facade_.canDisable(*id);
        if (!verdict.allowed) {
            statusBar()->showMessage(
                QString::fromStdString("cannot unbind: " + verdict.reason));
            return;
        }
        if (askConfirm(QStringLiteral("Unbind driver from %1? (not persistent)")
                           .arg(QString::fromStdString(device->name))))
            actions_.onUnbindDriver(*id);
    });

    bindAction_ = toolbar->addAction(QStringLiteral("Bind driver…"));
    connect(bindAction_, &QAction::triggered, this, [this] {
        const auto id = listVm_.selectedDeviceId();
        const auto device = id ? facade_.findById(*id) : std::nullopt;
        if (!device) return;
        QString prefill = QString::fromStdString(device->boundDriver.value_or(""));
        if (prefill.isEmpty()) {
            const auto candidates = facade_.driverInfo(*id);  // modalias dropdown data
            if (!candidates.empty()) prefill = QString::fromStdString(candidates.front().name);
        }
        const QString driver =
            actions_.textInput
                ? actions_.textInput(QStringLiteral("Bind driver"), prefill)
                : QInputDialog::getText(this, QStringLiteral("Bind driver"),
                                        QStringLiteral("Driver name:"), QLineEdit::Normal,
                                        prefill);
        static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
        if (!driver.isEmpty() && valid.match(driver).hasMatch())
            actions_.onBindDriver(*id, driver.toStdString());
    });
```

with the private helper `bool MainWindow::askConfirm(const QString& prompt)`:

```cpp
bool MainWindow::askConfirm(const QString& prompt) {
    return actions_.confirm ? actions_.confirm(prompt)
                            : QMessageBox::question(this, QStringLiteral("Confirm"), prompt) ==
                                  QMessageBox::Yes;
}
```

Central widget: build the Modules page and wrap both in tabs (replacing `setCentralWidget(splitter)`):

```cpp
    bannerLabel_ = new QLabel;
    moduleFilterEdit_ = new QLineEdit;
    moduleFilterEdit_->setPlaceholderText(QStringLiteral("filter modules…"));
    connect(moduleFilterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) { modulesVm_.setFilter(text.toStdString()); });
    moduleModel_ = new ModuleListModel(modulesVm_, this);
    modulesView_ = new QListView;
    modulesView_->setModel(moduleModel_);
    modulesView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    modulesView_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    moduleDetailTree_ = new QTreeWidget;
    moduleDetailTree_->setColumnCount(2);
    moduleDetailTree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    moduleDetailTree_->setRootIsDecorated(false);

    auto* modulesLeft = new QWidget;
    auto* modulesLeftLayout = new QVBoxLayout(modulesLeft);
    modulesLeftLayout->setContentsMargins(0, 0, 0, 0);
    modulesLeftLayout->addWidget(bannerLabel_);
    modulesLeftLayout->addWidget(moduleFilterEdit_);
    modulesLeftLayout->addWidget(modulesView_);
    auto* modulesSplitter = new QSplitter;
    modulesSplitter->addWidget(modulesLeft);
    modulesSplitter->addWidget(moduleDetailTree_);
    modulesSplitter->setStretchFactor(1, 1);

    tabs_ = new QTabWidget;
    tabs_->addTab(splitter, QStringLiteral("Devices"));
    tabs_->addTab(modulesSplitter, QStringLiteral("Modules"));
    setCentralWidget(tabs_);

    connect(tabs_, &QTabWidget::currentChanged, this, [this](int index) {
        updateActionEnablement();
        if (index == 1) {
            bannerLabel_->setText(QString::fromStdString(modulesVm_.banner()));
            modulesVm_.rebuild();
            modulesVm_.fillSignatures();
        }
    });
    connect(modulesView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) modulesVm_.selectedRef() = current.row();
                updateModuleDetailPane();
                updateActionEnablement();
            });
    connect(moduleModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateModuleDetailPane();
        updateActionEnablement();
    });
```

`updateModuleDetailPane()` mirrors `updateDetailPane()` over `modulesVm_.detailLines()` into `moduleDetailTree_` (same colon-split loop). `updateActionEnablement()` centralizes: `const bool onModules = tabs_->currentIndex() == 1;` module actions `setEnabled(onModules /*&& selection for unload*/)`, `toggleAction_/unbindAction_/bindAction_` enabled on the Devices tab with a selected device (fold the existing `updateToggleAction()` logic in; keep its advisory tooltip behavior). Call it from every place `updateToggleAction()` was called.

New includes in `main_window.cpp`: `<QInputDialog>`, `<QLabel>`, `<QRegularExpression>`, `<QTabWidget>`, `<QFontDatabase>`, `"gui/src/module_list_model.hpp"`.

- [x] **Step 3: gui_app.cpp wiring** — mirror the TUI root: construct `KmodDriverManager kmod;`, `LinuxSystemInfo sysinfo;`, pass `&kmod, &sysinfo` to the facade ctor, construct `app::ModulesVM modulesVm(facade, bus, scheduler, dispatcher);` next to the other VMs, and build `MainWindow::Actions`:

```cpp
    gui::MainWindow::Actions actions;
    actions.onRefresh = [&] { prunePending(); pending.push_back(facade.refresh()); };
    actions.onSetEnabled = [&](const core::DeviceId& id, bool enable) {
        prunePending();
        pending.push_back(facade.setDeviceEnabled(id, enable));
    };
    actions.onLoadModule = [&](const std::string& name) {
        prunePending();
        pending.push_back(facade.loadModule(name));
    };
    actions.onUnloadModule = [&](const std::string& name) {
        prunePending();
        pending.push_back(facade.unloadModule(name));
    };
    actions.onBindDriver = [&](const core::DeviceId& id, const std::string& driver) {
        prunePending();
        pending.push_back(facade.bindDriver(id, driver));
    };
    actions.onUnbindDriver = [&](const core::DeviceId& id) {
        prunePending();
        pending.push_back(facade.unbindDriver(id));
    };
```

(adapt names to gui_app.cpp's existing pending/prune infrastructure — it mirrors the TUI's; keep its teardown drain intact). Add `src/module_list_model.cpp` + header to `gui/CMakeLists.txt` (AUTOMOC picks up the Q_OBJECT).

- [x] **Step 4: Offscreen tests**

`gui/tests/test_module_list_model.cpp`:

```cpp
#include <QtTest/QtTest>

#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"
#include "gui/src/module_list_model.hpp"

// Follows gui/tests conventions (offscreen platform, gui_test_main.cpp).
class TestModuleListModel : public QObject {
    Q_OBJECT
   private slots:
    void rowsMirrorTheVm() {
        devmgr::runtime::EventBus bus;
        devmgr::runtime::TaskScheduler scheduler;
        devmgr::test::FakePal pal;
        devmgr::core::LoadedModule m;
        m.name = "dummy";
        pal.seedLoadedModule(m);
        devmgr::app::DeviceService service(bus);
        devmgr::test::InlineUiDispatcher dispatcher;
        devmgr::app::ApplicationFacade facade(pal, scheduler, bus, service, nullptr, nullptr,
                                              &pal, &pal);
        devmgr::app::ModulesVM vm(facade, bus, scheduler, dispatcher);
        devmgr::gui::ModuleListModel model(vm);
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(model.data(model.index(0, 0), Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("dummy")));
    }
};
```

(register with the existing offscreen test-main pattern used by `test_device_list_model.cpp` — same CMake target style, `#include "test_module_list_model.moc"` if the sibling tests do that.)

Append to `gui/tests/test_main_window.cpp` (adapt its fixture to the `Actions` struct — mechanical; its existing confirm-injection tests now set `actions.confirm`):

```cpp
    // Modules tab exists and gates the actions.
    QCOMPARE(window.tabs()->count(), 2);
    window.tabs()->setCurrentIndex(1);
    QVERIFY(window.loadModuleAction()->isEnabled());
    QVERIFY(!window.bindAction()->isEnabled());  // device actions off on Modules tab

    // Load flows through the injected text input + callback.
    QString captured;
    // (fixture) actions.textInput = [](const QString&, const QString&) {
    //     return QStringLiteral("dummy"); };
    // (fixture) actions.onLoadModule = [&](const std::string& n) {
    //     captured = QString::fromStdString(n); };
    window.loadModuleAction()->trigger();
    QCOMPARE(captured, QStringLiteral("dummy"));
```

- [x] **Step 5: Run GUI tests**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R gui --output-on-failure` (or the gui test target names used by Phase 3, e.g. `-R MainWindow|ModuleListModel`)
Expected: PASS offscreen.

- [x] **Step 6: Format, tidy, Commit (USER runs)**

```bash
git add -A && git commit -m "feat(gui): Phase 5 T12 — Modules tab (banner/list/detail/load/unload) + bind/unbind driver actions, Actions struct"
```

---

### Task 13: VM smoke script, docs, container gates, phase close-out

**Files:**
- Create: `test/vm/phase5-smoke.sh`
- Modify: `test-vm.sh`, `README.md`
- Modify: `docs/superpowers/plans/2026-07-06-phase5-driver-module-management.md` (checkboxes/execution notes), project memory files (execution status)

**Interfaces:** consumes the whole phase; produces the automated dangerous-op evidence (`PHASE5 VM SMOKE OK`) and the close-out state.

- [x] **Step 1: `test/vm/phase5-smoke.sh`**

```bash
#!/usr/bin/env bash
# Phase 5 dangerous E2E — run INSIDE the disposable VM as root, NEVER on a host.
# Usage: phase5-smoke.sh <sysfs path of a spare USB device> [sysfs path of a
# safe PCI/virtio device, e.g. the virtio balloon]. Root is implicitly
# polkit-authorized, so no agent is needed.
set -euo pipefail
USBDEV=${1:?usage: phase5-smoke.sh <usb device> [pci/virtio device]}
PCIDEV=${2:-}
[ -f "$USBDEV/authorized" ] || { echo "no authorized attr at $USBDEV"; exit 1; }

install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
rm -rf /var/lib/devmgrd   # clean slate for the persistence checks

start_daemon() {
    ./build/linux-debug/daemon/devmgrd &
    DPID=$!
    sleep 1
}
stop_daemon() { kill "$DPID" 2>/dev/null || true; wait "$DPID" 2>/dev/null || true; }
trap 'stop_daemon' EXIT
call() { busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 "$@"; }
start_daemon

echo "== module load/unload =="
call LoadModule s dummy
[ -d /sys/module/dummy ] || { echo "dummy did not load"; exit 1; }
call UnloadModule s dummy
[ ! -d /sys/module/dummy ] || { echo "dummy did not unload"; exit 1; }

echo "== blacklist refusal =="
echo "blacklist dummy" > /etc/modprobe.d/devmgr-smoke.conf
stop_daemon; start_daemon   # fresh kmod config
if call LoadModule s dummy 2>/tmp/blacklist.err; then
    echo "blacklisted load unexpectedly succeeded"; exit 1
fi
grep -q "blacklisted" /tmp/blacklist.err || { echo "wrong refusal:"; cat /tmp/blacklist.err; exit 1; }
rm /etc/modprobe.d/devmgr-smoke.conf

echo "== persistence: disable survives daemon restart + 'replug' =="
call SetDeviceEnabled sb "$USBDEV" false
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "disable did not stick"; exit 1; }
grep -q '"mechanism": "authorized"' /var/lib/devmgrd/state.json || {
    echo "state.json missing entry"; exit 1; }
stop_daemon
echo 1 > "$USBDEV/authorized"          # simulate the kernel re-enabling it
start_daemon                            # startup sweep must re-disable
for i in $(seq 1 50); do
    [ "$(cat "$USBDEV/authorized")" = "0" ] && break
    sleep 0.2
done
[ "$(cat "$USBDEV/authorized")" = "0" ] || { echo "sweep did not re-apply"; exit 1; }
call SetDeviceEnabled sb "$USBDEV" true
[ "$(cat "$USBDEV/authorized")" = "1" ] || { echo "enable did not stick"; exit 1; }

if [ -n "$PCIDEV" ] && [ -e "$PCIDEV/driver" ]; then
    echo "== unbind mechanism + surgical verbs on $PCIDEV =="
    DRIVER=$(basename "$(readlink -f "$PCIDEV/driver")")
    call SetDeviceEnabled sb "$PCIDEV" false
    [ ! -e "$PCIDEV/driver" ] || { echo "unbind-disable left a driver"; exit 1; }
    grep -q '"mechanism": "unbind"' /var/lib/devmgrd/state.json || {
        echo "unbind entry missing"; exit 1; }
    call SetDeviceEnabled sb "$PCIDEV" true      # driver_override targeted rebind
    [ -e "$PCIDEV/driver" ] || { echo "targeted rebind failed"; exit 1; }
    call UnbindDriver s "$PCIDEV"                # surgical: no state entry
    [ ! -e "$PCIDEV/driver" ] || { echo "surgical unbind failed"; exit 1; }
    grep -q '"entries": \[\]' /var/lib/devmgrd/state.json || {
        echo "surgical unbind polluted the store"; exit 1; }
    call BindDriver ss "$PCIDEV" "$DRIVER"
    [ -e "$PCIDEV/driver" ] || { echo "surgical bind failed"; exit 1; }
fi

echo "PHASE5 VM SMOKE OK"
```

`chmod +x test/vm/phase5-smoke.sh`. In `test-vm.sh`, after the Phase 4 line add:

```bash
echo "==> Running Phase 5 Smoke Test..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/cross-device-manager && sudo ./test/vm/phase5-smoke.sh /sys/bus/usb/devices/3-1 "$(ls -d /sys/bus/virtio/devices/virtio* 2>/dev/null | head -1)"')
```

- [x] **Step 2: README** — extend the feature list: universal persistent enable/disable with active enforcement, module load/unload with Secure Boot/lockdown awareness, Modules view in both UIs, surgical bind/unbind; note the new runtime deps (libkmod) and the daemon state dir (`/var/lib/devmgrd`, override with `--state-dir`).

- [x] **Step 3: Full gates**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Run: `clang-format --dry-run --Werror $(git diff feature/phase4 --name-only | grep -E '\.(hpp|cpp)$')`
USER runs (agent has no Docker daemon): `docker compose -f test/docker-compose.yml run --rm unit` and the tidy line from CI, plus `./test-vm.sh`.
Expected: unit suite green host+container, `PHASE4 VM SMOKE OK` **and** `PHASE5 VM SMOKE OK`.

- [x] **Step 4: Close-out** — mark all task checkboxes in this plan; update project memory (`phase5-execution-status`, `roadmap-and-active-phase`) with commits + exit-gate state.

- [x] **Step 5: Commit (USER runs)**

```bash
git add -A && git commit -m "test+docs: Phase 5 T13 — VM smoke (modules, persistence, surgical verbs), README, close-out"
```

---

## Exit Gate (host manual smoke — USER, on the Gentoo machine)

All on `feature/phase5`, devmgrd running in foreground as root, graphical polkit agent present:

1. **Modules view truthfulness** (both UIs): real module list; SIGNED column fills asynchronously (no first-paint stall); banner shows the machine's actual Secure Boot + lockdown state.
2. **Load/unload `dummy`** (both UIs): polkit prompt on first op, `auth_admin_keep` caches the second; status line reports "Loaded module dummy" / "Unloaded module dummy".
3. **Guard refusal**: attempt to unload the module backing the root NVMe (e.g. `nvme`) → clear reason, NO polkit prompt.
4. **Windows-grade persistence**: disable a spare USB device → physically replug it → it stays Disabled in the UI with no visible flicker; `state.json` holds the entry; restart devmgrd → still enforced.
5. **Surgical verbs**: unbind a safe device's driver (advanced action, advisory tooltip) → rebind via the candidates-prefilled dialog; confirm no persistence (replug returns it bound).
6. **Negative paths**: stop devmgrd → reads still work, mutations fail with the helper-unavailable message; disabled-PCI degradation renders as driverless (documented).
7. **TUI/GUI parity** on all of the above.

Phase 5 is DONE when: all 13 tasks committed, host+container suites green, `PHASE5 VM SMOKE OK`, and this checklist passes.







