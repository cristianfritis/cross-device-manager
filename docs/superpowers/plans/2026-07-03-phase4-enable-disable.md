# Phase 4 — Enable/Disable + privileged helper (devmgrd) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Safely disable/re-enable a non-critical USB device from either UI through a polkit-gated root helper (`devmgrd`) — the project's first mutating path.

**Architecture:** Frontends call `ApplicationFacade::setDeviceEnabled()` → worker task → `DbusPrivilegedChannel` (sdbus-c++ leaf #2) → D-Bus system bus → `devmgrd` (fourth composition root over `devmgr_core`), whose `RequestProcessor` pipeline runs validate → `CriticalDeviceGuard` (pure core policy × `LinuxCriticalityProber` facts) → polkit (`IAuthority` seam) → `SysfsDeviceController` USB `authorized` write. Every boundary has an injectable seam; the full IPC round-trip is tested on a `dbus-run-session` private bus.

**Tech Stack:** C++20, sdbus-c++ **v2 API only** (host: portage `dev-cpp/sdbus-c++ 2.3.1`; Docker: pinned v2.3.1 source build on Ubuntu 24.04), polkit via raw D-Bus `CheckAuthorization` (no libpolkit), GTest, CMake `linux-debug` preset.

**Spec:** `docs/superpowers/specs/2026-07-03-phase4-enable-disable-design.md` (approved + committed `f7b3813`, 2026-07-03).

## Resume State (fill in as tasks complete)

id|status|task|note
T1|✅ done|Core seams: criticality facts + guard + PAL signature changes|committed 1263cfc; tidy fixes in guard .cpp (starts_with/ranges/designated-init)
T2|✅ done|SysfsDeviceController + mapper authorized→Disabled|committed 5732ea1; plan-snippet fix: mapper compares sv(attr(d,"authorized")) == "0" (attr returns const char* — literal == would be pointer compare); mapper umockdev test container-only (runs at T10)
T3|✅ done|LinuxCriticalityProber|91/91 green (+7); tidy+format clean; plan-snippet deltas: probe() split → collectStorageFacts/collectInputFacts (cognitive-complexity ≤15 gate), rfind→starts_with, std::sort/unique→std::ranges::, consts kKeyQ/kKeyP/kBitsPerWord (magic-number gate), source[0]=='/' → source.starts_with('/'); awaiting user commit
T4|—|devmgrd_lib: IAuthority + RequestProcessor|
T5|—|StatusLineVM TaskCompleted + facade setDeviceEnabled/canDisable|
T6|—|sdbus gating + dbus_contract + ManagerAdaptor + PolkitAuthority + devmgrd|
T7|—|DbusPrivilegedChannel + IPC round-trip suite (tests/ipc)|
T8|—|TUI wiring ('e' toggle + confirm + auto-refresh)|
T9|—|GUI wiring (toggle action + confirm + auto-refresh)|
T10|—|Dockerfile sdbus build + CI guards + README + VM script|

Phase exit gate after T10: USER manual smoke (§ "Manual smoke" at the end) + VM dangerous script.

## Global Constraints

- **sdbus-c++ v2 API only.** Never use v1-isms (`registerMethod` directly on the object, pointer-returning `getCurrentlyProcessedMessage`). v2 uses strong types: `sdbus::ServiceName`, `sdbus::ObjectPath`, `sdbus::InterfaceName`, `sdbus::Error::Name`. Host has 2.3.1; user must `emerge dev-cpp/sdbus-c++` before T6.
- **The user commits every task; the agent is denied `git add`/`git commit`.** Each task's final step hands the user a suggested commit message and waits.
- **Purity:** no Qt/FTXUI include under `core/`, `app/`, `platform/`, `daemon/`. No sdbus-c++ include anywhere except `daemon/src/` and `platform/linux/src/dbus_privileged_channel.cpp` (T10 adds the CI grep). `core/` and `app/` never include sdbus or daemon headers.
- **Existing tests stay green:** host baseline is 70 (`devmgr_tests` + `devmgr_gui_tests` + gui selftest). Run the full suite every task, not just new tests.
- **Gates per task:** `cmake --build --preset linux-debug` clean; `ctest --test-dir build/linux-debug --output-on-failure` all pass; `clang-format --dry-run --Werror` clean on every touched file.
- **Include style:** repo-root-relative for non-installed headers (`#include "daemon/src/manager_adaptor.hpp"`); `devmgr/...` for public headers — matching `tui/`/`gui/`.
- **Naming:** namespaces `devmgr::services` (guard), `devmgr::daemon`; static libs `devmgrd_lib`; executable `devmgrd`; IPC test binary `devmgr_ipc_tests`.
- D-Bus contract constants (single source: `dbus_contract.hpp`, T6): bus name/interface `org.devmgr.Manager1`, object `/org/devmgr/Manager1`, action `org.devmgr.set-device-enabled`, `ApiVersion = 1`.
- Commands (repo root): `cmake --preset linux-debug` (re-run after CMake edits; after installing sdbus-c++ pass `-DDEVMGR_WITH_SDBUS=ON` once or delete `build/linux-debug/CMakeCache.txt`), `cmake --build --preset linux-debug`, `ctest --test-dir build/linux-debug --output-on-failure`.
- Env gotchas carried from Phase 3: `rtk` hook mangles some grep output — prefer Read/direct commands; clangd shows stale not-found on fresh files pre-configure — ignore; host clang-tidy dies on `-mno-direct-extern-access` for gui/ TUs only (sed workaround in memory `phase3-execution-status`) — daemon/platform TUs are unaffected.

## File Structure (end state)

```
daemon/
  CMakeLists.txt                       # devmgrd_lib (always) + devmgrd exe (sdbus-gated)
  include/devmgr/daemon/
    authority.hpp                      # IAuthority + AllowAll/DenyAll + CallerId (T4)
    request_processor.hpp              # validate→guard→authorize→act pipeline (T4)
  src/
    request_processor.cpp              # (T4)
    manager_adaptor.hpp/.cpp           # sdbus leaf #1: D-Bus ⇄ RequestProcessor (T6)
    polkit_authority.hpp/.cpp          # IAuthority via raw D-Bus CheckAuthorization (T6)
    main.cpp                           # flags: --bus --sysfs-root --mounts-path --authority (T6)
  data/
    org.devmgr.Manager1.conf           # D-Bus system policy (T6)
    org.devmgr.policy                  # polkit action, auth_admin_keep (T6)
core/include/devmgr/pal/criticality.hpp        # CriticalityFacts + ICriticalityProber (T1)
core/include/devmgr/services/critical_device_guard.hpp  # GuardVerdict + evaluateDisable (T1)
core/src/critical_device_guard.cpp             # (T1)
core/include/devmgr/pal/interfaces.hpp         # IDeviceController/IPrivilegedChannel new sigs (T1)
platform/linux/include/devmgr/platform/linux/
    sysfs_device_controller.hpp        # (T2)
    linux_criticality_prober.hpp       # (T3)
    dbus_contract.hpp                  # names + error mapping, NO sdbus include (T6)
    dbus_privileged_channel.hpp        # (T7)
platform/linux/src/
    sysfs_device_controller.cpp        # (T2)
    linux_criticality_prober.cpp       # (T3)
    dbus_privileged_channel.cpp        # sdbus leaf #2 (T7)
    udev_device_mapper.cpp             # authorized==0 → Disabled (T2)
app/include/devmgr/app/application_facade.hpp  # + setDeviceEnabled/canDisable (T5)
app/src/application_facade.cpp                 # (T5)
app/include/devmgr/app/status_line_vm.hpp      # + TaskCompleted subscription (T5)
app/src/status_line_vm.cpp                     # (T5)
tests/fakes/fake_pal.hpp                       # setEnabled(path) rekey (T1)
tests/fakes/fake_privileged_channel.hpp        # (T5)
tests/fakes/fake_criticality_prober.hpp        # (T5)
tests/unit/test_critical_device_guard.cpp      # (T1)
tests/unit/test_sysfs_device_controller.cpp    # (T2)
tests/unit/test_linux_criticality_prober.cpp   # (T3)
tests/unit/test_request_processor.cpp          # (T4)
tests/unit/test_dbus_contract.cpp              # (T6)
tests/ipc/CMakeLists.txt + test_ipc_round_trip.cpp   # private-bus round trip (T7)
tui/src/tui_app.cpp                            # 'e' + confirm + auto-refresh (T8)
gui/src/main_window.hpp/.cpp, gui/src/gui_app.cpp, gui/tests/test_main_window.cpp  # (T9)
test/vm/phase4-smoke.sh                        # dangerous VM script (T10)
Dockerfile, .github/workflows/ci.yml, README.md  # (T10)
CMakeLists.txt                                 # daemon subdir (T4); sdbus gating + tests/ipc (T6)
```

---

### Task 1: Core seams — `CriticalityFacts`, `CriticalDeviceGuard`, PAL signature changes

Pure core additions plus the two free interface-signature changes (no implementations exist yet). `FakePal` re-keys on `sysfsPath`.

**Files:**
- Create: `core/include/devmgr/pal/criticality.hpp`
- Create: `core/include/devmgr/services/critical_device_guard.hpp`
- Create: `core/src/critical_device_guard.cpp`
- Modify: `core/CMakeLists.txt` (add source)
- Modify: `core/include/devmgr/pal/interfaces.hpp:30-34,64-68`
- Modify: `tests/fakes/fake_pal.hpp` (setEnabled signature + key)
- Modify: `tests/unit/test_fake_pal.cpp`
- Create: `tests/unit/test_critical_device_guard.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::Result`, `core::Device`.
- Produces (T2–T9 depend on these exact signatures):
  - `devmgr::pal::CriticalityFacts { std::vector<std::string> rootBackingPaths, bootBackingPaths, keyboardPaths, pointerPaths; }`
  - `devmgr::pal::ICriticalityProber::probe() -> core::Result<CriticalityFacts>`
  - `devmgr::services::GuardVerdict { bool allowed = true; std::string reason; }`
  - `devmgr::services::evaluateDisable(const pal::CriticalityFacts&, const std::string& targetSysfsPath) -> GuardVerdict`
  - `pal::IDeviceController::setEnabled(const std::string& sysfsPath, bool enabled)` (was `DeviceId`)
  - `pal::IPrivilegedChannel::setDeviceEnabled(const core::Device& device, bool enabled)` (was `DeviceId`)

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_critical_device_guard.cpp`:

```cpp
#include <gtest/gtest.h>

#include "devmgr/services/critical_device_guard.hpp"

using devmgr::pal::CriticalityFacts;
using devmgr::services::evaluateDisable;

namespace {
CriticalityFacts facts() {
    CriticalityFacts f;
    f.rootBackingPaths = {"/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0/0:0:0:0"};
    f.bootBackingPaths = {"/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0/0:0:0:0"};
    f.keyboardPaths = {"/sys/devices/pci0000:00/usb1/1-3/1-3:1.0/input/input5"};
    f.pointerPaths = {"/sys/devices/pci0000:00/usb1/1-4/1-4:1.0/input/input6"};
    return f;
}
}  // namespace

TEST(CriticalDeviceGuardTest, AllowsUnrelatedDevice) {
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/usb1/1-9");
    EXPECT_TRUE(v.allowed);
    EXPECT_TRUE(v.reason.empty());
}

TEST(CriticalDeviceGuardTest, RefusesRootBackingAncestor) {
    // Disabling the ATA controller would take the root disk with it.
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/0000:00:1f.2");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "backs the root filesystem");
}

TEST(CriticalDeviceGuardTest, RefusesExactRootBackingPath) {
    const auto v = evaluateDisable(
        facts(), "/sys/devices/pci0000:00/0000:00:1f.2/ata1/host0/target0/0:0:0:0");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "backs the root filesystem");
}

TEST(CriticalDeviceGuardTest, PathBoundaryIsRespected) {
    // "1-1" must not match a root-backing path under "1-10" (prefix-with-boundary).
    CriticalityFacts f;
    f.rootBackingPaths = {"/sys/devices/pci0000:00/usb1/1-10/1-10:1.0/host7/block/sdb"};
    EXPECT_TRUE(evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-1").allowed);
    EXPECT_FALSE(evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-10").allowed);
}

TEST(CriticalDeviceGuardTest, RefusesSoleKeyboard) {
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/usb1/1-3");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "would disable the only keyboard");
}

TEST(CriticalDeviceGuardTest, AllowsKeyboardWhenAnotherRemains) {
    auto f = facts();
    f.keyboardPaths.push_back("/sys/devices/platform/i8042/serio0/input/input1");
    EXPECT_TRUE(evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-3").allowed);
}

TEST(CriticalDeviceGuardTest, RefusesSolePointer) {
    const auto v = evaluateDisable(facts(), "/sys/devices/pci0000:00/usb1/1-4");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "would disable the only pointer");
}

TEST(CriticalDeviceGuardTest, NoFactsMeansAllowed) {
    EXPECT_TRUE(evaluateDisable(CriticalityFacts{}, "/sys/devices/pci0000:00/usb1/1-3").allowed);
}

TEST(CriticalDeviceGuardTest, RootRefusalWinsOverInputRefusal) {
    // A USB disk that both backs / and hosts the only keyboard: root reason first.
    CriticalityFacts f;
    f.rootBackingPaths = {"/sys/devices/pci0000:00/usb1/1-3/1-3:1.0/host7/block/sdb"};
    f.keyboardPaths = {"/sys/devices/pci0000:00/usb1/1-3/1-3:1.1/input/input5"};
    const auto v = evaluateDisable(f, "/sys/devices/pci0000:00/usb1/1-3");
    EXPECT_FALSE(v.allowed);
    EXPECT_EQ(v.reason, "backs the root filesystem");
}
```

Register it — in `tests/CMakeLists.txt` add to the `add_executable(devmgr_tests ...)` list after `unit/test_status_line_vm.cpp`:

```cmake
    unit/test_critical_device_guard.cpp
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `'devmgr/services/critical_device_guard.hpp' file not found` (compile failure is this step's "failing test").

- [ ] **Step 3: Implement**

Create `core/include/devmgr/pal/criticality.hpp`:

```cpp
#pragma once
#include <string>
#include <vector>

#include "devmgr/core/result.hpp"

namespace devmgr::pal {

// Inputs to services::evaluateDisable. All entries are canonical sysfs device
// paths (symlinks resolved), so they are prefix-comparable with a target
// device's sysfsPath. Gathered fresh per check — never cached.
struct CriticalityFacts {
    std::vector<std::string> rootBackingPaths;  // devices backing the / filesystem
    std::vector<std::string> bootBackingPaths;  // devices backing /boot (empty if none)
    std::vector<std::string> keyboardPaths;     // live keyboard input devices
    std::vector<std::string> pointerPaths;      // live pointer input devices
};

// Platform contract that gathers CriticalityFacts. Linux impl:
// platform_linux::LinuxCriticalityProber. Injected into devmgrd
// (authoritative) and ApplicationFacade (advisory).
class ICriticalityProber {
   public:
    virtual ~ICriticalityProber() = default;
    virtual core::Result<CriticalityFacts> probe() = 0;
};

}  // namespace devmgr::pal
```

Create `core/include/devmgr/services/critical_device_guard.hpp`:

```cpp
#pragma once
#include <string>

#include "devmgr/pal/criticality.hpp"

namespace devmgr::services {

struct GuardVerdict {
    bool allowed = true;
    std::string reason;  // set when refused
};

// Pure policy (Phase 4 spec): may the device at targetSysfsPath be DISABLED?
// Refuses when the target subtree contains a root/boot backing device or the
// sole remaining keyboard/pointer. Used authoritatively by devmgrd and
// advisorily by the frontends — same function, one behavior.
GuardVerdict evaluateDisable(const pal::CriticalityFacts& facts,
                             const std::string& targetSysfsPath);

}  // namespace devmgr::services
```

Create `core/src/critical_device_guard.cpp`:

```cpp
#include "devmgr/services/critical_device_guard.hpp"

#include <algorithm>
#include <vector>

namespace devmgr::services {
namespace {

// True iff `path` equals `prefix` or lies inside it, honoring the '/'
// boundary ("/sys/a/1-1" is NOT under "/sys/a/1-10").
bool isUnder(const std::string& path, const std::string& prefix) {
    if (prefix.empty() || path.size() < prefix.size()) return false;
    if (path.compare(0, prefix.size(), prefix) != 0) return false;
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

bool anyUnder(const std::vector<std::string>& paths, const std::string& target) {
    return std::any_of(paths.begin(), paths.end(),
                       [&](const std::string& p) { return isUnder(p, target); });
}

// True iff disabling `target` would remove EVERY entry in `paths` (and there
// is at least one to remove) — the "sole remaining keyboard/pointer" rule.
bool wouldRemoveAll(const std::vector<std::string>& paths, const std::string& target) {
    if (paths.empty()) return false;
    return std::all_of(paths.begin(), paths.end(),
                       [&](const std::string& p) { return isUnder(p, target); });
}

}  // namespace

GuardVerdict evaluateDisable(const pal::CriticalityFacts& facts,
                             const std::string& targetSysfsPath) {
    if (anyUnder(facts.rootBackingPaths, targetSysfsPath))
        return {false, "backs the root filesystem"};
    if (anyUnder(facts.bootBackingPaths, targetSysfsPath))
        return {false, "backs the boot filesystem"};
    if (wouldRemoveAll(facts.keyboardPaths, targetSysfsPath))
        return {false, "would disable the only keyboard"};
    if (wouldRemoveAll(facts.pointerPaths, targetSysfsPath))
        return {false, "would disable the only pointer"};
    return {};
}

}  // namespace devmgr::services
```

In `core/CMakeLists.txt`, add to the `add_library(devmgr_core ...)` list after `src/delayed_scheduler.cpp`:

```cmake
    src/critical_device_guard.cpp
```

In `core/include/devmgr/pal/interfaces.hpp`, replace the `IDeviceController` and `IPrivilegedChannel` bodies:

```cpp
class IDeviceController {
   public:
    virtual ~IDeviceController() = default;
    // Identity is the device's canonical sysfs path — the wire format devmgrd
    // receives and the coordinate SysfsDeviceController acts on. Phase 4
    // mechanism: USB `authorized` only (non-USB → Error::Unsupported).
    virtual core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) = 0;
};
```

```cpp
class IPrivilegedChannel {
   public:
    virtual ~IPrivilegedChannel() = default;
    // Takes the full Device (the channel needs sysfsPath on the wire and name
    // for messages). Blocking: interactive polkit auth may take ~minutes —
    // call from a TaskScheduler worker, never a UI thread.
    virtual core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) = 0;
};
```

In `tests/fakes/fake_pal.hpp`, re-key enable state on `sysfsPath`. Replace `seedDevice`, `enabled`, and `setEnabled`:

```cpp
    void seedDevice(core::Device device) {
        enabled_[device.sysfsPath] = true;
        devices_.push_back(std::move(device));
    }
```

```cpp
    bool enabled(const std::string& sysfsPath) const {
        const auto it = enabled_.find(sysfsPath);
        return it != enabled_.end() && it->second;
    }
```

```cpp
    core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) override {
        const auto it = enabled_.find(sysfsPath);
        if (it == enabled_.end())
            return core::makeError(core::Error::Code::NotFound, "no such device: " + sysfsPath);
        it->second = enabled;
        return {};
    }
```

In `tests/unit/test_fake_pal.cpp`, replace the two controller tests (seeded devices now need a `sysfsPath`):

```cpp
TEST(FakePal, SetEnabledUpdatesState) {
    FakePal pal;
    Device d{DeviceId{"usb:1-2"}};
    d.sysfsPath = "/sys/devices/usb1/1-2";
    pal.seedDevice(d);
    IDeviceController& controller = pal;
    ASSERT_TRUE(controller.setEnabled("/sys/devices/usb1/1-2", false).has_value());
    EXPECT_FALSE(pal.enabled("/sys/devices/usb1/1-2"));
}

TEST(FakePal, SetEnabledOnUnknownDeviceReturnsNotFound) {
    FakePal pal;
    IDeviceController& controller = pal;
    auto result = controller.setEnabled("/sys/devices/ghost", false);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, devmgr::core::Error::Code::NotFound);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (baseline 70 + 9 guard tests = 79; FakePal tests still green).

- [ ] **Step 5: Format check**

Run: `clang-format --dry-run --Werror core/include/devmgr/pal/criticality.hpp core/include/devmgr/services/critical_device_guard.hpp core/src/critical_device_guard.cpp core/include/devmgr/pal/interfaces.hpp tests/fakes/fake_pal.hpp tests/unit/test_fake_pal.cpp tests/unit/test_critical_device_guard.cpp`
Expected: no output.

- [ ] **Step 6: Hand off for commit (user commits — agent must not)**

Suggested message: `feat(core): CriticalityFacts + CriticalDeviceGuard pure policy; path-identity for IDeviceController/IPrivilegedChannel`

---

### Task 2: `SysfsDeviceController` + mapper `authorized==0 → Disabled`

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp`
- Create: `platform/linux/src/sysfs_device_controller.cpp`
- Modify: `platform/linux/CMakeLists.txt`
- Modify: `platform/linux/src/udev_device_mapper.cpp:88`
- Modify: `tests/integration/test_udev_enumerator.cpp` (container-verified)
- Create: `tests/unit/test_sysfs_device_controller.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pal::IDeviceController` (T1 path signature).
- Produces: `platform_linux::SysfsDeviceController(std::string sysfsRoot = "/sys")` implementing `setEnabled(sysfsPath, enabled)`. Error contract: `NotFound` (unresolvable/outside root/missing dir), `Unsupported` (no `authorized` attr), `Io` (open/write failure).

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_sysfs_device_controller.cpp`:

```cpp
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "devmgr/platform/linux/sysfs_device_controller.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using devmgr::platform_linux::SysfsDeviceController;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

class SysfsControllerTest : public ::testing::Test {
   protected:
    fs::path root_;
    fs::path device_;
    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-sysfs-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        device_ = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device_);
        std::ofstream(device_ / "authorized") << "1";
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
};
}  // namespace

TEST_F(SysfsControllerTest, DisableWritesZeroAndEnableWritesOne) {
    SysfsDeviceController controller(root_.string());
    ASSERT_TRUE(controller.setEnabled(device_.string(), false).has_value());
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
    ASSERT_TRUE(controller.setEnabled(device_.string(), true).has_value());
    EXPECT_EQ(readFile(device_ / "authorized"), "1");
}

TEST_F(SysfsControllerTest, MissingDeviceIsNotFound) {
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled((root_ / "devices/ghost").string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, PathOutsideRootIsNotFound) {
    SysfsDeviceController controller((root_ / "devices").string());
    // ".." escape attempts must be rejected after canonicalization.
    auto r = controller.setEnabled((root_ / "devices/../..").string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(SysfsControllerTest, NoAuthorizedAttrIsUnsupported) {
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:02.0";
    fs::create_directories(pci);
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled(pci.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}

TEST_F(SysfsControllerTest, UnwritableAttrIsIo) {
    if (::geteuid() == 0) GTEST_SKIP() << "root ignores file permissions";
    fs::permissions(device_ / "authorized", fs::perms::owner_read);
    SysfsDeviceController controller(root_.string());
    auto r = controller.setEnabled(device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
}
```

In `tests/CMakeLists.txt`, extend the existing `if(UNIX AND NOT APPLE)` block:

```cmake
if(UNIX AND NOT APPLE)
    # Header-only udev mapping helpers (NO libudev link) — unit-test them directly.
    target_include_directories(devmgr_tests PRIVATE ${CMAKE_SOURCE_DIR}/platform/linux/include)
    # Linux-only concrete PAL units (controller/prober) — these DO link the PAL lib.
    target_sources(devmgr_tests PRIVATE unit/test_sysfs_device_controller.cpp)
    target_link_libraries(devmgr_tests PRIVATE devmgr_pal_linux)
endif()
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `'devmgr/platform/linux/sysfs_device_controller.hpp' file not found`.

- [ ] **Step 3: Implement**

Create `platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp`:

```cpp
#pragma once
#include <string>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// IDeviceController over sysfs. Phase 4 mechanism: the USB `authorized`
// attribute only — the one path whose disabled-state round-trips purely
// through sysfs (spec 2026-07-03). Runs in-process in devmgrd (as root);
// the sysfs root is injectable so tests drive a fake tree in a temp dir.
class SysfsDeviceController final : public pal::IDeviceController {
   public:
    explicit SysfsDeviceController(std::string sysfsRoot = "/sys");
    core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) override;

   private:
    std::string sysfsRoot_;
};

}  // namespace devmgr::platform_linux
```

Create `platform/linux/src/sysfs_device_controller.cpp`:

```cpp
#include "devmgr/platform/linux/sysfs_device_controller.hpp"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {
// Canonical containment: `path` must resolve to a directory at or below
// `root`. Rejects symlink/".." escapes — the daemon trusts nothing from the
// client (spec: validate before guard/auth/act).
bool isContained(const fs::path& canonicalPath, const fs::path& canonicalRoot) {
    const auto rel = canonicalPath.lexically_relative(canonicalRoot);
    return !rel.empty() && rel.native().rfind("..", 0) != 0;
}
}  // namespace

SysfsDeviceController::SysfsDeviceController(std::string sysfsRoot)
    : sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<void> SysfsDeviceController::setEnabled(const std::string& sysfsPath, bool enabled) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    if (ec || !isContained(canonical, root))
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound, "device no longer present: " + sysfsPath);
    const fs::path attr = canonical / "authorized";
    if (!fs::exists(attr, ec))
        return core::makeError(
            core::Error::Code::Unsupported,
            "enable/disable not supported for this device (no authorized attribute)");
    std::ofstream out(attr);
    if (!out)
        return core::makeError(core::Error::Code::Io,
                               "cannot open " + attr.string() + ": " +
                                   std::generic_category().message(errno));
    out << (enabled ? '1' : '0');
    out.flush();
    if (!out)
        return core::makeError(core::Error::Code::Io, "write failed: " + attr.string());
    return {};
}

}  // namespace devmgr::platform_linux
```

In `platform/linux/CMakeLists.txt`, add to `add_library(devmgr_pal_linux ...)` after `src/udev_device_mapper.cpp`:

```cmake
    src/sysfs_device_controller.cpp
```

In `platform/linux/src/udev_device_mapper.cpp`, replace line 88 (`dev.status = core::DeviceStatus::Active;`) with:

```cpp
    // Phase 4: a deauthorized USB device (authorized == "0") is Disabled;
    // everything else stays Active as before.
    dev.status = (dev.bus == core::BusType::Usb && attr(d, "authorized") == "0")
                     ? core::DeviceStatus::Disabled
                     : core::DeviceStatus::Active;
```

Append to `tests/integration/test_udev_enumerator.cpp` (container-only umockdev suite — verifies the mapper end-to-end):

```cpp
TEST_F(UdevEnumeratorTest, DeauthorizedUsbDeviceMapsToDisabled) {
    gchar* sys = umockdev_testbed_add_device(bed_, "usb", "1-9", nullptr, "authorized", "0",
                                             "idVendor", "abcd", "idProduct", "ef01", nullptr,
                                             "SUBSYSTEM", "usb", nullptr);
    ASSERT_NE(sys, nullptr);
    g_free(sys);

    devmgr::platform_linux::UdevDeviceEnumerator enumr;
    auto res = enumr.enumerate();
    ASSERT_TRUE(res.has_value()) << res.error().message;
    auto it = std::find_if(res->begin(), res->end(), [](const auto& d) {
        return d.vendorId == "abcd" && d.productId == "ef01";
    });
    ASSERT_NE(it, res->end());
    EXPECT_EQ(it->status, devmgr::core::DeviceStatus::Disabled);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (+5 controller tests; the mapper test runs in the container suite at T10, not here).

- [ ] **Step 5: Format check**

Run: `clang-format --dry-run --Werror platform/linux/include/devmgr/platform/linux/sysfs_device_controller.hpp platform/linux/src/sysfs_device_controller.cpp platform/linux/src/udev_device_mapper.cpp tests/unit/test_sysfs_device_controller.cpp tests/integration/test_udev_enumerator.cpp`
Expected: no output.

- [ ] **Step 6: Hand off for commit**

Suggested message: `feat(platform): SysfsDeviceController (USB authorized, injectable root); mapper maps authorized==0 to Disabled`

---

### Task 3: `LinuxCriticalityProber`

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/linux_criticality_prober.hpp`
- Create: `platform/linux/src/linux_criticality_prober.cpp`
- Modify: `platform/linux/CMakeLists.txt`
- Create: `tests/unit/test_linux_criticality_prober.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pal::ICriticalityProber` / `pal::CriticalityFacts` (T1).
- Produces: `platform_linux::LinuxCriticalityProber(std::string sysfsRoot = "/sys", std::string mountsPath = "/proc/self/mounts")`. Facts semantics: block sources resolved via `<sysfsRoot>/class/block/<name>` with recursive `slaves/` expansion (dm/RAID); inputs classified from `<sysfsRoot>/class/input/input*/capabilities/{key,rel}`; all paths `realpath`-canonical; non-`/dev` mount sources skipped.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_linux_criticality_prober.cpp`:

```cpp
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "devmgr/platform/linux/linux_criticality_prober.hpp"

namespace fs = std::filesystem;
using devmgr::platform_linux::LinuxCriticalityProber;

namespace {
// Builds a miniature sysfs+/dev+mounts world in a temp dir. Layout mirrors
// real sysfs: class/{block,input} entries are symlinks into devices/.
class ProberTest : public ::testing::Test {
   protected:
    fs::path root_;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-prober-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::create_directories(root_ / "class/block");
        fs::create_directories(root_ / "class/input");
        fs::create_directories(root_ / "dev");
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    // A block device living under `devicesSubdir`, linked from class/block
    // and from /dev. Returns the canonical device dir.
    fs::path addBlock(const std::string& name, const std::string& devicesSubdir) {
        const fs::path dir = root_ / "devices" / devicesSubdir / "block" / name;
        fs::create_directories(dir);
        fs::create_directory_symlink(dir, root_ / "class/block" / name);
        std::ofstream(root_ / "dev" / name) << "";  // realpath target for mounts source
        return fs::canonical(dir);
    }

    fs::path addInput(const std::string& name, const std::string& devicesSubdir,
                      const std::string& keyBits, const std::string& relBits) {
        const fs::path dir = root_ / "devices" / devicesSubdir / "input" / name;
        fs::create_directories(dir / "capabilities");
        std::ofstream(dir / "capabilities/key") << keyBits;
        std::ofstream(dir / "capabilities/rel") << relBits;
        fs::create_directory_symlink(dir, root_ / "class/input" / name);
        return fs::canonical(dir);
    }

    void writeMounts(const std::string& content) {
        std::ofstream(root_ / "mounts") << content;
    }

    LinuxCriticalityProber prober() {
        return LinuxCriticalityProber(root_.string(), (root_ / "mounts").string());
    }
};

// capabilities/key with KEY_Q..KEY_P (codes 16-25) set = 0x3ff0000; a real
// keyboard line has more words — the parser must index from the RIGHT.
constexpr const char* kKeyboardKey = "3ff0000";
constexpr const char* kNoKeys = "0";
}  // namespace

TEST_F(ProberTest, RootMountResolvesToBlockDevicePath) {
    const auto sda = addBlock("sda1", "pci0000:00/ata1/host0");
    writeMounts(root_.string() + "/dev/sda1 / ext4 rw 0 0\n");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value()) << facts.error().message;
    ASSERT_EQ(facts->rootBackingPaths.size(), 1u);
    EXPECT_EQ(facts->rootBackingPaths[0], sda.string());
    EXPECT_TRUE(facts->bootBackingPaths.empty());
}

TEST_F(ProberTest, BootMountIsCollectedSeparately) {
    addBlock("sda1", "pci0000:00/ata1/host0");
    const auto sda2 = addBlock("sda2", "pci0000:00/ata1/host0");
    writeMounts(root_.string() + "/dev/sda1 / ext4 rw 0 0\n" +
                root_.string() + "/dev/sda2 /boot ext4 rw 0 0\n");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->bootBackingPaths.size(), 1u);
    EXPECT_EQ(facts->bootBackingPaths[0], sda2.string());
}

TEST_F(ProberTest, DmDeviceExpandsThroughSlaves) {
    // dm-0 is virtual; its slaves/ entry names the physical sda2.
    const auto sda2 = addBlock("sda2", "pci0000:00/usb1/1-4/host7");
    const fs::path dm = root_ / "devices/virtual/block/dm-0";
    fs::create_directories(dm / "slaves");
    fs::create_directory_symlink(root_ / "class/block/sda2", dm / "slaves/sda2");
    fs::create_directory_symlink(dm, root_ / "class/block/dm-0");
    std::ofstream(root_ / "dev/dm-0") << "";
    writeMounts(root_.string() + "/dev/dm-0 / ext4 rw 0 0\n");

    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->rootBackingPaths.size(), 1u);
    EXPECT_EQ(facts->rootBackingPaths[0], sda2.string());
}

TEST_F(ProberTest, NonDevSourcesAreSkipped) {
    writeMounts("tmpfs / tmpfs rw 0 0\n");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    EXPECT_TRUE(facts->rootBackingPaths.empty());
}

TEST_F(ProberTest, ClassifiesKeyboardAndPointer) {
    const auto kb = addInput("input5", "pci0000:00/usb1/1-3/1-3:1.0", kKeyboardKey, "0");
    const auto mouse = addInput("input6", "pci0000:00/usb1/1-4/1-4:1.0", kNoKeys, "3");
    writeMounts("");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->keyboardPaths.size(), 1u);
    EXPECT_EQ(facts->keyboardPaths[0], kb.string());
    ASSERT_EQ(facts->pointerPaths.size(), 1u);
    EXPECT_EQ(facts->pointerPaths[0], mouse.string());
}

TEST_F(ProberTest, MultiWordKeyBitmapIndexesFromTheRight) {
    // Real keyboards print many words: "... 3ff0000 0 0 0" style. Codes 16-25
    // live in the LAST (least significant) word.
    const auto kb =
        addInput("input7", "platform/i8042/serio0", "fffffffff 3ff0000", "0");
    writeMounts("");
    auto facts = prober().probe();
    ASSERT_TRUE(facts.has_value());
    ASSERT_EQ(facts->keyboardPaths.size(), 1u);
    EXPECT_EQ(facts->keyboardPaths[0], kb.string());
}

TEST_F(ProberTest, MissingMountsFileIsAnError) {
    auto facts = LinuxCriticalityProber(root_.string(), (root_ / "nope").string()).probe();
    EXPECT_FALSE(facts.has_value());
}
```

In `tests/CMakeLists.txt`, add inside the `if(UNIX AND NOT APPLE)` block's `target_sources`:

```cmake
    target_sources(devmgr_tests PRIVATE
        unit/test_sysfs_device_controller.cpp
        unit/test_linux_criticality_prober.cpp)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: BUILD FAILURE — missing `linux_criticality_prober.hpp`.

- [ ] **Step 3: Implement**

Create `platform/linux/include/devmgr/platform/linux/linux_criticality_prober.hpp`:

```cpp
#pragma once
#include <string>

#include "devmgr/pal/criticality.hpp"

namespace devmgr::platform_linux {

// Gathers CriticalityFacts from /proc/self/mounts + sysfs (spec 2026-07-03):
// - root/boot backing devices: mount source → /dev realpath → basename →
//   <sysfsRoot>/class/block/<name>, expanded recursively through slaves/
//   (dm/LUKS/RAID) down to physical leaves; recorded as canonical paths.
// - keyboards/pointers: <sysfsRoot>/class/input/input*, classified from
//   capabilities/key (KEY_Q..KEY_P all present → keyboard) and
//   capabilities/rel (REL_X+REL_Y → pointer).
// Non-/dev mount sources (tmpfs, network, zfs pools) are skipped — documented
// residual: such roots yield no storage facts. Probe fresh per check.
class LinuxCriticalityProber final : public pal::ICriticalityProber {
   public:
    explicit LinuxCriticalityProber(std::string sysfsRoot = "/sys",
                                    std::string mountsPath = "/proc/self/mounts");
    core::Result<pal::CriticalityFacts> probe() override;

   private:
    std::string sysfsRoot_;
    std::string mountsPath_;
};

}  // namespace devmgr::platform_linux
```

Create `platform/linux/src/linux_criticality_prober.cpp`:

```cpp
#include "devmgr/platform/linux/linux_criticality_prober.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;

namespace {

std::string readFirstLine(const fs::path& p) {
    std::ifstream in(p);
    std::string line;
    std::getline(in, line);
    return line;
}

// capabilities files are hex words, MOST significant first, 64-bit each on
// this platform (unsigned long). Bit `bit` counted from LSB of the LAST word.
bool bitSet(const std::string& hexWords, unsigned bit) {
    std::vector<std::string> words;
    std::istringstream in(hexWords);
    for (std::string w; in >> w;) words.push_back(w);
    const std::size_t wordIdx = bit / 64;
    if (wordIdx >= words.size()) return false;
    const std::string& word = words[words.size() - 1 - wordIdx];
    std::uint64_t value = 0;
    std::istringstream(word) >> std::hex >> value;
    return ((value >> (bit % 64)) & 1U) != 0U;
}

// Keyboard heuristic (same spirit as udev's input_id): the full QWERTY top
// row (KEY_Q=16 .. KEY_P=25) is present.
bool isKeyboard(const std::string& keyBits) {
    for (unsigned code = 16; code <= 25; ++code)
        if (!bitSet(keyBits, code)) return false;
    return !keyBits.empty();
}

// Pointer: relative X and Y axes (REL_X=0, REL_Y=1).
bool isPointer(const std::string& relBits) {
    return !relBits.empty() && bitSet(relBits, 0) && bitSet(relBits, 1);
}

// Expand a block-device name to its physical leaves through slaves/
// (virtual dm/md devices sit under devices/virtual and would never prefix-
// match a real controller path — their slaves do).
void expandBlock(const fs::path& classBlock, const std::string& name,
                 std::vector<std::string>& out) {
    const fs::path dir = classBlock / name;
    std::error_code ec;
    const fs::path slaves = dir / "slaves";
    if (fs::is_directory(slaves, ec) && !fs::is_empty(slaves, ec)) {
        for (const auto& entry : fs::directory_iterator(slaves, ec))
            expandBlock(classBlock, entry.path().filename().string(), out);
        return;
    }
    const fs::path real = fs::canonical(dir, ec);
    if (!ec) out.push_back(real.string());
}

void sortUnique(std::vector<std::string>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

}  // namespace

LinuxCriticalityProber::LinuxCriticalityProber(std::string sysfsRoot, std::string mountsPath)
    : sysfsRoot_(std::move(sysfsRoot)), mountsPath_(std::move(mountsPath)) {}

core::Result<pal::CriticalityFacts> LinuxCriticalityProber::probe() {
    std::ifstream mounts(mountsPath_);
    if (!mounts)
        return core::makeError(core::Error::Code::Io, "cannot read " + mountsPath_);

    pal::CriticalityFacts facts;
    const fs::path classBlock = fs::path(sysfsRoot_) / "class/block";

    for (std::string line; std::getline(mounts, line);) {
        std::istringstream fields(line);
        std::string source;
        std::string target;
        if (!(fields >> source >> target)) continue;
        std::vector<std::string>* bucket = nullptr;
        if (target == "/") bucket = &facts.rootBackingPaths;
        if (target == "/boot" || target == "/boot/efi") bucket = &facts.bootBackingPaths;
        if (bucket == nullptr || source.empty() || source[0] != '/') continue;
        std::error_code ec;
        const fs::path node = fs::canonical(fs::path(source), ec);  // /dev/mapper/x → /dev/dm-0
        if (ec) continue;
        expandBlock(classBlock, node.filename().string(), *bucket);
    }

    std::error_code ec;
    const fs::path classInput = fs::path(sysfsRoot_) / "class/input";
    if (fs::is_directory(classInput, ec)) {
        for (const auto& entry : fs::directory_iterator(classInput, ec)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("input", 0) != 0) continue;
            const fs::path real = fs::canonical(entry.path(), ec);
            if (ec) continue;
            const std::string keyBits = readFirstLine(real / "capabilities/key");
            const std::string relBits = readFirstLine(real / "capabilities/rel");
            if (isKeyboard(keyBits)) facts.keyboardPaths.push_back(real.string());
            if (isPointer(relBits)) facts.pointerPaths.push_back(real.string());
        }
    }

    sortUnique(facts.rootBackingPaths);
    sortUnique(facts.bootBackingPaths);
    sortUnique(facts.keyboardPaths);
    sortUnique(facts.pointerPaths);
    return facts;
}

}  // namespace devmgr::platform_linux
```

In `platform/linux/CMakeLists.txt`, add to the library sources after `src/sysfs_device_controller.cpp`:

```cmake
    src/linux_criticality_prober.cpp
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (+7 prober tests).

- [ ] **Step 5: Format check**

Run: `clang-format --dry-run --Werror platform/linux/include/devmgr/platform/linux/linux_criticality_prober.hpp platform/linux/src/linux_criticality_prober.cpp tests/unit/test_linux_criticality_prober.cpp`
Expected: no output.

- [ ] **Step 6: Hand off for commit**

Suggested message: `feat(platform): LinuxCriticalityProber — root/boot block ancestry (slaves-walk) + keyboard/pointer classification`

---

### Task 4: `devmgrd_lib` — `IAuthority` + `RequestProcessor`

The daemon's logic layer, bus-free and fully unit-tested. `devmgrd_lib` builds unconditionally (no sdbus).

**Files:**
- Create: `daemon/CMakeLists.txt`
- Create: `daemon/include/devmgr/daemon/authority.hpp`
- Create: `daemon/include/devmgr/daemon/request_processor.hpp`
- Create: `daemon/src/request_processor.cpp`
- Modify: `CMakeLists.txt` (root — add `add_subdirectory(daemon)`)
- Create: `tests/unit/test_request_processor.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pal::IDeviceController` (T1), `pal::ICriticalityProber` (T1), `services::evaluateDisable` (T1).
- Produces (T6 depends on these exact signatures):
  - `devmgr::daemon::CallerId` = `std::string` (caller's unique D-Bus bus name)
  - `devmgr::daemon::IAuthority::checkAuthorized(const CallerId&, const std::string& actionId) -> core::Result<bool>`
  - `devmgr::daemon::AllowAllAuthority`, `devmgr::daemon::DenyAllAuthority`
  - `devmgr::daemon::kActionSetDeviceEnabled` = `"org.devmgr.set-device-enabled"`
  - `devmgr::daemon::RequestProcessor(pal::IDeviceController&, pal::ICriticalityProber&, IAuthority&, std::string sysfsRoot = "/sys")` with `setDeviceEnabled(const CallerId&, const std::string& sysfsPath, bool enabled) -> core::Result<void>`

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/test_request_processor.cpp`:

```cpp
#include <unistd.h>

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/request_processor.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using namespace devmgr;

namespace {

class RecordingController final : public pal::IDeviceController {
   public:
    core::Result<void> setEnabled(const std::string& sysfsPath, bool enabled) override {
        calls.push_back({sysfsPath, enabled});
        return next;
    }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    core::Result<void> next = {};
};

class StubProber final : public pal::ICriticalityProber {
   public:
    core::Result<pal::CriticalityFacts> probe() override {
        ++probes;
        return next;
    }
    int probes = 0;
    core::Result<pal::CriticalityFacts> next = pal::CriticalityFacts{};
};

class StubAuthority final : public daemon::IAuthority {
   public:
    core::Result<bool> checkAuthorized(const daemon::CallerId& caller,
                                       const std::string& actionId) override {
        ++checks;
        lastAction = actionId;
        lastCaller = caller;
        return next;
    }
    int checks = 0;
    std::string lastAction;
    daemon::CallerId lastCaller;
    core::Result<bool> next = true;
};

// The processor canonicalizes and containment-checks paths itself, so the
// tests need a real directory to point at.
class RequestProcessorTest : public ::testing::Test {
   protected:
    fs::path root_;
    fs::path device_;
    RecordingController controller_;
    StubProber prober_;
    StubAuthority authority_;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-reqproc-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        device_ = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    daemon::RequestProcessor processor() {
        return daemon::RequestProcessor(controller_, prober_, authority_, root_.string());
    }
};

}  // namespace

TEST_F(RequestProcessorTest, HappyPathDisablesViaControllerWithCanonicalPath) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.42", device_.string(), false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(controller_.calls.size(), 1u);
    EXPECT_EQ(controller_.calls[0].sysfsPath, fs::weakly_canonical(device_).string());
    EXPECT_FALSE(controller_.calls[0].enabled);
    EXPECT_EQ(authority_.checks, 1);
    EXPECT_EQ(authority_.lastAction, daemon::kActionSetDeviceEnabled);
    EXPECT_EQ(authority_.lastCaller, ":1.42");
}

TEST_F(RequestProcessorTest, GuardRefusalShortCircuitsBeforeAuthorityAndController) {
    pal::CriticalityFacts f;
    f.rootBackingPaths = {fs::weakly_canonical(device_).string() + "/host0/block/sdb"};
    prober_.next = f;
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Conflict);
    EXPECT_EQ(r.error().message, "backs the root filesystem");
    EXPECT_EQ(authority_.checks, 0);  // no password prompt for a doomed request
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, EnableSkipsGuardButStillAuthorizes) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), true);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(prober_.probes, 0);  // re-enabling can't hurt — guard not consulted
    EXPECT_EQ(authority_.checks, 1);
}

TEST_F(RequestProcessorTest, DeniedAuthorityIsPermissionAndBlocksController) {
    authority_.next = false;
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Permission);
    EXPECT_EQ(r.error().message, "authorization denied");
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, AuthorityErrorPropagates) {
    authority_.next = core::makeError(Error::Code::Io, "polkit unavailable");
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, PathOutsideRootIsNotFoundBeforeEverything) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", "/etc/passwd", false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(prober_.probes, 0);
    EXPECT_EQ(authority_.checks, 0);
    EXPECT_TRUE(controller_.calls.empty());
}

TEST_F(RequestProcessorTest, MissingDeviceDirIsNotFound) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", (root_ / "devices/ghost").string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(RequestProcessorTest, ProberErrorPropagatesOnDisable) {
    prober_.next = core::makeError(Error::Code::Io, "mounts unreadable");
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", device_.string(), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_EQ(authority_.checks, 0);
}
```

In `tests/CMakeLists.txt` (inside the `if(UNIX AND NOT APPLE)` block):

```cmake
    target_sources(devmgr_tests PRIVATE
        unit/test_sysfs_device_controller.cpp
        unit/test_linux_criticality_prober.cpp
        unit/test_request_processor.cpp)
    target_link_libraries(devmgr_tests PRIVATE devmgr_pal_linux devmgrd_lib)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --preset linux-debug`
Expected: CMake generate error — `devmgrd_lib` target unknown (the daemon subdir doesn't exist yet). Same failing-state semantics as prior phases' create-file tasks.

- [ ] **Step 3: Implement**

Create `daemon/include/devmgr/daemon/authority.hpp`:

```cpp
#pragma once
#include <string>

#include "devmgr/core/result.hpp"

namespace devmgr::daemon {

// A caller's unique D-Bus bus name (e.g. ":1.42") — the subject polkit
// authorizes. ManagerAdaptor extracts it from the method-call message.
using CallerId = std::string;

// Authorization seam: PolkitAuthority in production, Allow/DenyAll under
// --authority test flags (spec: full pipeline testable without a polkit agent).
class IAuthority {
   public:
    virtual ~IAuthority() = default;
    // True = authorized. Interactive authentication may block for ~minutes.
    virtual core::Result<bool> checkAuthorized(const CallerId& caller,
                                               const std::string& actionId) = 0;
};

class AllowAllAuthority final : public IAuthority {
   public:
    core::Result<bool> checkAuthorized(const CallerId&, const std::string&) override {
        return true;
    }
};

class DenyAllAuthority final : public IAuthority {
   public:
    core::Result<bool> checkAuthorized(const CallerId&, const std::string&) override {
        return false;
    }
};

}  // namespace devmgr::daemon
```

Create `daemon/include/devmgr/daemon/request_processor.hpp`:

```cpp
#pragma once
#include <string>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::daemon {

inline constexpr const char* kActionSetDeviceEnabled = "org.devmgr.set-device-enabled";

// The daemon's verb pipeline: validate → guard → authorize → act (spec
// 2026-07-03). Pure logic, no D-Bus types — ManagerAdaptor translates.
// Trusts nothing from the client: the path is canonicalized and containment-
// checked here, criticality facts are probed fresh per request, and the guard
// runs BEFORE authorization so a refused request never triggers a password
// prompt. The guard applies to disable only; re-enabling skips it.
class RequestProcessor {
   public:
    RequestProcessor(pal::IDeviceController& controller, pal::ICriticalityProber& prober,
                     IAuthority& authority, std::string sysfsRoot = "/sys");

    core::Result<void> setDeviceEnabled(const CallerId& caller, const std::string& sysfsPath,
                                        bool enabled);

   private:
    pal::IDeviceController& controller_;
    pal::ICriticalityProber& prober_;
    IAuthority& authority_;
    std::string sysfsRoot_;
};

}  // namespace devmgr::daemon
```

Create `daemon/src/request_processor.cpp`:

```cpp
#include "devmgr/daemon/request_processor.hpp"

#include <filesystem>
#include <utility>

#include "devmgr/services/critical_device_guard.hpp"

namespace devmgr::daemon {
namespace fs = std::filesystem;

RequestProcessor::RequestProcessor(pal::IDeviceController& controller,
                                   pal::ICriticalityProber& prober, IAuthority& authority,
                                   std::string sysfsRoot)
    : controller_(controller),
      prober_(prober),
      authority_(authority),
      sysfsRoot_(std::move(sysfsRoot)) {}

core::Result<void> RequestProcessor::setDeviceEnabled(const CallerId& caller,
                                                      const std::string& sysfsPath,
                                                      bool enabled) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(fs::path(sysfsPath), ec);
    if (ec) return core::makeError(core::Error::Code::NotFound, "cannot resolve " + sysfsPath);
    const fs::path root = fs::weakly_canonical(fs::path(sysfsRoot_), ec);
    const auto rel = canonical.lexically_relative(root);
    if (ec || rel.empty() || rel.native().rfind("..", 0) == 0)
        return core::makeError(core::Error::Code::NotFound,
                               "path outside sysfs root: " + sysfsPath);
    if (!fs::is_directory(canonical, ec))
        return core::makeError(core::Error::Code::NotFound,
                               "device no longer present: " + sysfsPath);

    if (!enabled) {
        auto facts = prober_.probe();
        if (!facts) return tl::unexpected(facts.error());
        const auto verdict = services::evaluateDisable(*facts, canonical.string());
        if (!verdict.allowed)
            return core::makeError(core::Error::Code::Conflict, verdict.reason);
    }

    auto authorized = authority_.checkAuthorized(caller, kActionSetDeviceEnabled);
    if (!authorized) return tl::unexpected(authorized.error());
    if (!*authorized)
        return core::makeError(core::Error::Code::Permission, "authorization denied");

    return controller_.setEnabled(canonical.string(), enabled);
}

}  // namespace devmgr::daemon
```

Create `daemon/CMakeLists.txt`:

```cmake
# devmgrd_lib: the daemon's bus-free logic (RequestProcessor + authorities).
# Built unconditionally so its unit tests always run; the devmgrd executable
# itself (main + sdbus adaptor + polkit) is added behind DEVMGR_WITH_SDBUS in
# Task 6.
add_library(devmgrd_lib STATIC
    src/request_processor.cpp)
target_include_directories(devmgrd_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgrd_lib PUBLIC devmgr_core)
target_compile_features(devmgrd_lib PUBLIC cxx_std_20)
```

In the root `CMakeLists.txt`, inside the `if(UNIX AND NOT APPLE)` block, after `add_subdirectory(platform/linux)`:

```cmake
    add_subdirectory(daemon)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (+8 request-processor tests).

- [ ] **Step 5: Format check**

Run: `clang-format --dry-run --Werror daemon/include/devmgr/daemon/authority.hpp daemon/include/devmgr/daemon/request_processor.hpp daemon/src/request_processor.cpp tests/unit/test_request_processor.cpp`
Expected: no output.

- [ ] **Step 6: Hand off for commit**

Suggested message: `feat(daemon): devmgrd_lib — IAuthority seam + RequestProcessor validate→guard→authorize→act pipeline`

---

### Task 5: `StatusLineVM` task messages + `ApplicationFacade::setDeviceEnabled`/`canDisable`

The only `devmgr_app` changes of the phase — toolkit-agnostic, mirroring Phase 3's discipline.

**Files:**
- Modify: `app/include/devmgr/app/status_line_vm.hpp` (one member)
- Modify: `app/src/status_line_vm.cpp` (one subscription + one reset)
- Modify: `app/include/devmgr/app/application_facade.hpp`
- Modify: `app/src/application_facade.cpp`
- Create: `tests/fakes/fake_privileged_channel.hpp`
- Create: `tests/fakes/fake_criticality_prober.hpp`
- Modify: `tests/unit/test_application_facade.cpp`
- Modify: `tests/unit/test_status_line_vm.cpp`

**Interfaces:**
- Consumes: `pal::IPrivilegedChannel` (T1 Device signature), `pal::ICriticalityProber`, `services::evaluateDisable` (T1), `core::TaskCompletedEvent`.
- Produces (T8/T9 depend on these exact signatures):
  - `ApplicationFacade(pal::IDeviceEnumerator&, runtime::TaskScheduler&, runtime::EventBus&, DeviceService&, pal::IPrivilegedChannel* channel = nullptr, pal::ICriticalityProber* prober = nullptr)` — existing 4-arg call sites keep compiling.
  - `std::future<void> ApplicationFacade::setDeviceEnabled(const core::DeviceId& id, bool enabled)` — same future-custody contract as `refresh()`; publishes exactly one `TaskCompletedEvent{taskId = "set-enabled:" + id.value, ok, message}`.
  - `services::GuardVerdict ApplicationFacade::canDisable(const core::DeviceId& id) const` — advisory; allowed when prober is null/failing or the device is unknown.
  - `StatusLineVM` now shows `TaskCompletedEvent::message` (armed-gated, same TTL clear).
  - `test::FakePrivilegedChannel{ calls: vector<{sysfsPath,enabled}>, next: Result<void> }`, `test::FakeCriticalityProber{ next: Result<CriticalityFacts> }`.

- [ ] **Step 1: Write the failing tests**

Create `tests/fakes/fake_privileged_channel.hpp`:

```cpp
#pragma once
#include <string>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// Records channel calls; `next` scripts the outcome.
class FakePrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override {
        calls.push_back({device.sysfsPath, enabled});
        return next;
    }
    struct Call {
        std::string sysfsPath;
        bool enabled;
    };
    std::vector<Call> calls;
    core::Result<void> next = {};
};

}  // namespace devmgr::test
```

Create `tests/fakes/fake_criticality_prober.hpp`:

```cpp
#pragma once
#include "devmgr/pal/criticality.hpp"

namespace devmgr::test {

class FakeCriticalityProber final : public pal::ICriticalityProber {
   public:
    core::Result<pal::CriticalityFacts> probe() override { return next; }
    core::Result<pal::CriticalityFacts> next = pal::CriticalityFacts{};
};

}  // namespace devmgr::test
```

Append to `tests/unit/test_application_facade.cpp` (note: its `dev()` helper gets a `sysfsPath`-setting sibling):

```cpp
namespace {
core::Device devAt(std::string id, std::string sysfsPath, std::string name) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.sysfsPath = std::move(sysfsPath);
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}
}  // namespace

TEST(ApplicationFacadeTest, SetDeviceEnabledCallsChannelAndPublishesOneCompletion) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    test::FakeCriticalityProber prober;
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, &prober);
    facade.refresh().wait();
    facade.setDeviceEnabled(core::DeviceId{"u1"}, false).wait();

    ASSERT_EQ(channel.calls.size(), 1u);
    EXPECT_EQ(channel.calls[0].sysfsPath, "/sys/devices/usb1/1-4");
    EXPECT_FALSE(channel.calls[0].enabled);
    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].taskId, "set-enabled:u1");
    EXPECT_TRUE(events[0].ok);
    EXPECT_EQ(events[0].message, "Disabled Webcam");
}

TEST(ApplicationFacadeTest, SetDeviceEnabledFailureCarriesChannelErrorMessage) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    channel.next = core::makeError(core::Error::Code::Permission, "authorization denied");
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr);
    facade.refresh().wait();
    facade.setDeviceEnabled(core::DeviceId{"u1"}, false).wait();

    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "authorization denied");
}

TEST(ApplicationFacadeTest, SetDeviceEnabledWithoutChannelIsUnsupported) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-4", "Webcam"));
    app::DeviceService svc(bus);
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);  // 4-arg form still compiles
    facade.refresh().wait();
    facade.setDeviceEnabled(core::DeviceId{"u1"}, false).wait();

    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "built without privileged-helper support");
}

TEST(ApplicationFacadeTest, SetDeviceEnabledUnknownIdReportsGoneWithoutChannelCall) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    test::FakePrivilegedChannel channel;
    std::vector<core::TaskCompletedEvent> events;
    std::mutex m;
    auto sub = bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
        std::scoped_lock lock(m);
        events.push_back(e);
    });

    app::ApplicationFacade facade(pal, scheduler, bus, svc, &channel, nullptr);
    facade.setDeviceEnabled(core::DeviceId{"ghost"}, false).wait();

    EXPECT_TRUE(channel.calls.empty());
    std::scoped_lock lock(m);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_FALSE(events[0].ok);
    EXPECT_EQ(events[0].message, "device no longer present");
}

TEST(ApplicationFacadeTest, CanDisableConsultsGuardThroughProber) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-3", "Keyboard"));
    app::DeviceService svc(bus);
    test::FakeCriticalityProber prober;
    pal::CriticalityFacts facts;
    facts.keyboardPaths = {"/sys/devices/usb1/1-3/1-3:1.0/input/input5"};
    prober.next = facts;

    app::ApplicationFacade facade(pal, scheduler, bus, svc, nullptr, &prober);
    facade.refresh().wait();

    const auto verdict = facade.canDisable(core::DeviceId{"u1"});
    EXPECT_FALSE(verdict.allowed);
    EXPECT_EQ(verdict.reason, "would disable the only keyboard");
}

TEST(ApplicationFacadeTest, CanDisableIsAllowedWithoutProberOrOnProbeError) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(devAt("u1", "/sys/devices/usb1/1-3", "Keyboard"));
    app::DeviceService svc(bus);

    app::ApplicationFacade noProber(pal, scheduler, bus, svc);
    noProber.refresh().wait();
    EXPECT_TRUE(noProber.canDisable(core::DeviceId{"u1"}).allowed);

    test::FakeCriticalityProber failing;
    failing.next = core::makeError(core::Error::Code::Io, "mounts unreadable");
    app::ApplicationFacade withFailing(pal, scheduler, bus, svc, nullptr, &failing);
    withFailing.refresh().wait();
    EXPECT_TRUE(withFailing.canDisable(core::DeviceId{"u1"}).allowed);
}
```

Add the includes those tests need at the top of `tests/unit/test_application_facade.cpp` (after the existing includes):

```cpp
#include <mutex>
#include <vector>

#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_privileged_channel.hpp"
```

Append to `tests/unit/test_status_line_vm.cpp` (NOTE: this file has no `using namespace devmgr;` — fully qualify, matching its existing style; it already includes `fakes/inline_ui_dispatcher.hpp`):

```cpp
TEST(StatusLineVm, TaskCompletedMessageShownAfterArmOnly) {
    devmgr::runtime::EventBus bus;
    devmgr::runtime::DelayedScheduler timer;
    devmgr::test::InlineUiDispatcher dispatcher;
    devmgr::app::StatusLineVM vm(bus, timer, dispatcher);

    bus.publish(devmgr::core::TaskCompletedEvent{"t0", true, "ignored before arm"});
    EXPECT_EQ(vm.text(), "");

    vm.arm();
    bus.publish(
        devmgr::core::TaskCompletedEvent{"t1", false, "cannot disable: sole keyboard"});
    EXPECT_EQ(vm.text(), "cannot disable: sole keyboard");
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `no member named 'setDeviceEnabled' in 'devmgr::app::ApplicationFacade'`.

- [ ] **Step 3: Implement**

`app/include/devmgr/app/status_line_vm.hpp` — add one member next to `subChanged_`:

```cpp
    runtime::Subscription subTaskCompleted_;
```

`app/src/status_line_vm.cpp` — in the constructor after the `subRemoved_` subscription:

```cpp
    subTaskCompleted_ =
        bus.subscribe<core::TaskCompletedEvent>([this](const core::TaskCompletedEvent& e) {
            // Mutation results (Phase 4) share the transient line — success and
            // failure alike; the message already says which.
            if (armed_.load()) setMessage(e.message);
        });
```

And in the destructor, alongside the other resets:

```cpp
    subTaskCompleted_.reset();
```

`app/include/devmgr/app/application_facade.hpp` — replace the class body's public section and members:

```cpp
class ApplicationFacade {
   public:
    // channel/prober are optional (null in sdbus-less builds): without a
    // channel setDeviceEnabled reports Unsupported; without a prober
    // canDisable is advisory-unavailable and answers "allowed" (devmgrd
    // remains authoritative).
    ApplicationFacade(pal::IDeviceEnumerator& enumerator, runtime::TaskScheduler& scheduler,
                      runtime::EventBus& bus, DeviceService& service,
                      pal::IPrivilegedChannel* channel = nullptr,
                      pal::ICriticalityProber* prober = nullptr)
        : enumerator_(enumerator),
          scheduler_(scheduler),
          bus_(bus),
          service_(service),
          channel_(channel),
          prober_(prober) {}

    // Runs enumeration on the TaskScheduler. The caller MUST wait on (or get)
    // the returned future before destroying this facade — the worker task
    // captures `this`, so discarding the future and destroying the facade would
    // dereference a dangling pointer.
    std::future<void> refresh();

    // Phase 4 mutation: resolves the device, calls the privileged channel on a
    // worker, publishes exactly ONE TaskCompletedEvent{taskId =
    // "set-enabled:" + id.value} — success and every failure mode alike.
    // Same future-custody contract as refresh(). The channel call may block
    // for ~minutes on interactive polkit auth; never wait on the UI thread.
    std::future<void> setDeviceEnabled(const core::DeviceId& id, bool enabled);

    // Advisory guard for UX (grey-out/annotate): pure core policy over
    // freshly probed facts. devmgrd re-checks authoritatively on every
    // request — this result is never a substitute for that.
    services::GuardVerdict canDisable(const core::DeviceId& id) const;

    std::vector<core::Device> devices() const { return service_.devices(); }
    std::optional<core::Device> findById(const core::DeviceId& id) const {
        return service_.findById(id);
    }

   private:
    pal::IDeviceEnumerator& enumerator_;
    runtime::TaskScheduler& scheduler_;
    runtime::EventBus& bus_;
    DeviceService& service_;
    pal::IPrivilegedChannel* channel_ = nullptr;
    pal::ICriticalityProber* prober_ = nullptr;
};
```

Add to the header's includes:

```cpp
#include "devmgr/pal/criticality.hpp"
#include "devmgr/services/critical_device_guard.hpp"
```

`app/src/application_facade.cpp` — append:

```cpp
std::future<void> ApplicationFacade::setDeviceEnabled(const core::DeviceId& id, bool enabled) {
    return scheduler_.submit([this, id, enabled] {
        const std::string taskId = "set-enabled:" + id.value;
        const auto device = service_.findById(id);  // resolve at execution time
        if (!device) {
            bus_.publish(core::TaskCompletedEvent{taskId, false, "device no longer present"});
            return;
        }
        if (channel_ == nullptr) {
            bus_.publish(core::TaskCompletedEvent{taskId, false,
                                                  "built without privileged-helper support"});
            return;
        }
        auto result = channel_->setDeviceEnabled(*device, enabled);
        if (result) {
            bus_.publish(core::TaskCompletedEvent{
                taskId, true, (enabled ? "Enabled " : "Disabled ") + device->name});
        } else {
            bus_.publish(core::TaskCompletedEvent{taskId, false, result.error().message});
        }
    });
}

services::GuardVerdict ApplicationFacade::canDisable(const core::DeviceId& id) const {
    if (prober_ == nullptr) return {};
    const auto device = service_.findById(id);
    if (!device) return {};
    auto facts = prober_->probe();
    if (!facts) return {};  // advisory unavailable → allowed; daemon is authoritative
    return services::evaluateDisable(*facts, device->sysfsPath);
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (+7 new tests; existing facade/status tests untouched).

- [ ] **Step 5: Format check**

Run: `clang-format --dry-run --Werror app/include/devmgr/app/application_facade.hpp app/src/application_facade.cpp app/include/devmgr/app/status_line_vm.hpp app/src/status_line_vm.cpp tests/fakes/fake_privileged_channel.hpp tests/fakes/fake_criticality_prober.hpp tests/unit/test_application_facade.cpp tests/unit/test_status_line_vm.cpp`
Expected: no output.

- [ ] **Step 6: Hand off for commit**

Suggested message: `feat(app): ApplicationFacade::setDeviceEnabled/canDisable + StatusLineVM task-result messages`

---

### Task 6: sdbus gating, `dbus_contract.hpp`, `ManagerAdaptor`, `PolkitAuthority`, `devmgrd`

**Prerequisite (user):** `sudo emerge dev-cpp/sdbus-c++` (2.3.1). Everything below is gated so the tree still builds without it.

**Files:**
- Modify: `CMakeLists.txt` (root — sdbus detection + option)
- Create: `platform/linux/include/devmgr/platform/linux/dbus_contract.hpp` (NO sdbus include)
- Create: `tests/unit/test_dbus_contract.cpp`
- Modify: `tests/CMakeLists.txt`
- Create: `daemon/src/manager_adaptor.hpp`, `daemon/src/manager_adaptor.cpp` (sdbus leaf #1)
- Create: `daemon/src/polkit_authority.hpp`, `daemon/src/polkit_authority.cpp`
- Create: `daemon/src/main.cpp`
- Modify: `daemon/CMakeLists.txt`
- Create: `daemon/data/org.devmgr.Manager1.conf`, `daemon/data/org.devmgr.policy`

**Interfaces:**
- Consumes: `RequestProcessor`, `IAuthority`, authorities (T4); `SysfsDeviceController` (T2); `LinuxCriticalityProber` (T3).
- Produces (T7 depends on these):
  - `platform_linux::kBusName = "org.devmgr.Manager1"`, `kObjectPath = "/org/devmgr/Manager1"`, `kInterfaceName = "org.devmgr.Manager1"`, `kApiVersion = 1u`
  - `platform_linux::dbusErrorNameFor(core::Error::Code) -> const char*` and `platform_linux::coreErrorFor(const std::string& dbusErrorName, std::string message) -> core::Error` (the spec's error table, both directions)
  - `devmgrd` binary with flags `--bus system|session`, `--sysfs-root <path>`, `--mounts-path <path>`, `--authority polkit|allow-all|deny-all`

- [ ] **Step 1: Write the failing tests (error-mapping round trip)**

Create `tests/unit/test_dbus_contract.cpp`:

```cpp
#include <gtest/gtest.h>

#include "devmgr/platform/linux/dbus_contract.hpp"

using devmgr::core::Error;
using namespace devmgr::platform_linux;

TEST(DbusContractTest, DomainCodesRoundTripThroughErrorNames) {
    const Error::Code codes[] = {Error::Code::Conflict, Error::Code::Permission,
                                 Error::Code::NotFound, Error::Code::Unsupported,
                                 Error::Code::Io};
    for (const auto code : codes) {
        const auto mapped = coreErrorFor(dbusErrorNameFor(code), "msg");
        EXPECT_EQ(mapped.code, code);
        EXPECT_EQ(mapped.message, "msg");
    }
}

TEST(DbusContractTest, BusyAndNetworkCollapseToIoOnTheWire) {
    EXPECT_STREQ(dbusErrorNameFor(Error::Code::Busy), kErrIo);
    EXPECT_STREQ(dbusErrorNameFor(Error::Code::Network), kErrIo);
}

TEST(DbusContractTest, TransportErrorsMapPerSpecTable) {
    const auto gone = coreErrorFor("org.freedesktop.DBus.Error.ServiceUnknown", "x");
    EXPECT_EQ(gone.code, Error::Code::Io);
    EXPECT_EQ(gone.message, "helper devmgrd is not available");

    const auto slow = coreErrorFor("org.freedesktop.DBus.Error.NoReply", "x");
    EXPECT_EQ(slow.code, Error::Code::Busy);
    EXPECT_EQ(slow.message, "helper timed out");

    const auto other = coreErrorFor("org.freedesktop.DBus.Error.NoMemory", "boom");
    EXPECT_EQ(other.code, Error::Code::Io);
    EXPECT_EQ(other.message, "boom");
}
```

Register in `tests/CMakeLists.txt` (inside the `if(UNIX AND NOT APPLE)` `target_sources` list): `unit/test_dbus_contract.cpp`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: BUILD FAILURE — missing `dbus_contract.hpp`.

- [ ] **Step 3: Implement the contract header**

Create `platform/linux/include/devmgr/platform/linux/dbus_contract.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <utility>

#include "devmgr/core/result.hpp"

namespace devmgr::platform_linux {

// The Phase 4 IPC contract (spec 2026-07-03) — names + the error table, shared
// by devmgrd (throw side) and DbusPrivilegedChannel (catch side). Pure
// strings: NO sdbus-c++ include here (CI purity guard).
inline constexpr const char* kBusName = "org.devmgr.Manager1";
inline constexpr const char* kObjectPath = "/org/devmgr/Manager1";
inline constexpr const char* kInterfaceName = "org.devmgr.Manager1";
inline constexpr std::uint32_t kApiVersion = 1;

inline constexpr const char* kErrCritical = "org.devmgr.Error.CriticalDevice";
inline constexpr const char* kErrNotAuthorized = "org.devmgr.Error.NotAuthorized";
inline constexpr const char* kErrNotFound = "org.devmgr.Error.NotFound";
inline constexpr const char* kErrUnsupported = "org.devmgr.Error.Unsupported";
inline constexpr const char* kErrIo = "org.devmgr.Error.Io";

inline const char* dbusErrorNameFor(core::Error::Code code) {
    switch (code) {
        case core::Error::Code::Conflict:
            return kErrCritical;
        case core::Error::Code::Permission:
            return kErrNotAuthorized;
        case core::Error::Code::NotFound:
            return kErrNotFound;
        case core::Error::Code::Unsupported:
            return kErrUnsupported;
        default:  // Io, Busy, Network collapse to Io on the wire
            return kErrIo;
    }
}

inline core::Error coreErrorFor(const std::string& dbusErrorName, std::string message) {
    if (dbusErrorName == kErrCritical)
        return {core::Error::Code::Conflict, std::move(message)};
    if (dbusErrorName == kErrNotAuthorized)
        return {core::Error::Code::Permission, std::move(message)};
    if (dbusErrorName == kErrNotFound)
        return {core::Error::Code::NotFound, std::move(message)};
    if (dbusErrorName == kErrUnsupported)
        return {core::Error::Code::Unsupported, std::move(message)};
    if (dbusErrorName == "org.freedesktop.DBus.Error.ServiceUnknown")
        return {core::Error::Code::Io, "helper devmgrd is not available"};
    if (dbusErrorName == "org.freedesktop.DBus.Error.NoReply" ||
        dbusErrorName == "org.freedesktop.DBus.Error.Timeout")
        return {core::Error::Code::Busy, "helper timed out"};
    return {core::Error::Code::Io, std::move(message)};
}

}  // namespace devmgr::platform_linux
```

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R DbusContract --output-on-failure`
Expected: 3 tests PASS.

- [ ] **Step 4: sdbus CMake gating**

Root `CMakeLists.txt`, inside `if(UNIX AND NOT APPLE)` after the Qt6 block:

```cmake
    # devmgrd + DbusPrivilegedChannel: auto-ON where sdbus-c++ >= 2 is present.
    # v2 API only (v2.0 was a breaking rewrite): host = portage 2.3.1, Docker =
    # pinned v2.3.1 source build — see the Phase 4 spec. The tree must still
    # configure and build with no sdbus-c++ installed.
    find_package(sdbus-c++ 2 QUIET)
    option(DEVMGR_WITH_SDBUS "Build devmgrd + the D-Bus privileged channel" ${sdbus-c++_FOUND})
```

`daemon/CMakeLists.txt`, append:

```cmake
if(DEVMGR_WITH_SDBUS)
    add_executable(devmgrd
        src/main.cpp
        src/manager_adaptor.cpp
        src/polkit_authority.cpp)
    # Repo-root-relative includes for the private src/ headers, matching gui/.
    target_include_directories(devmgrd PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(devmgrd PRIVATE devmgrd_lib devmgr_pal_linux SDBusCpp::sdbus-c++)
endif()
```

- [ ] **Step 5: Implement the daemon**

Create `daemon/src/manager_adaptor.hpp`:

```cpp
#pragma once
#include <memory>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/daemon/request_processor.hpp"

namespace devmgr::daemon {

// sdbus-c++ LEAF FILE #1 (with main.cpp/polkit_authority.cpp): translates
// org.devmgr.Manager1 D-Bus calls into RequestProcessor calls and Result
// errors into named D-Bus errors. No logic of its own.
class ManagerAdaptor {
   public:
    ManagerAdaptor(sdbus::IConnection& connection, RequestProcessor& processor);

   private:
    RequestProcessor& processor_;
    std::unique_ptr<sdbus::IObject> object_;
};

}  // namespace devmgr::daemon
```

Create `daemon/src/manager_adaptor.cpp`:

```cpp
#include "daemon/src/manager_adaptor.hpp"

#include <string>

#include "devmgr/platform/linux/dbus_contract.hpp"

namespace devmgr::daemon {

ManagerAdaptor::ManagerAdaptor(sdbus::IConnection& connection, RequestProcessor& processor)
    : processor_(processor) {
    object_ = sdbus::createObject(connection,
                                  sdbus::ObjectPath{platform_linux::kObjectPath});
    object_
        ->addVTable(
            sdbus::registerMethod("SetDeviceEnabled")
                .withInputParamNames("sysfs_path", "enabled")
                .implementedAs([this](const std::string& sysfsPath, const bool enabled) {
                    // getCurrentlyProcessedMessage is only valid inside the
                    // handler — the polkit subject is the caller's unique name.
                    const auto sender =
                        std::string{object_->getCurrentlyProcessedMessage().getSender()};
                    auto result = processor_.setDeviceEnabled(sender, sysfsPath, enabled);
                    if (!result) {
                        throw sdbus::Error(
                            sdbus::Error::Name{
                                platform_linux::dbusErrorNameFor(result.error().code)},
                            result.error().message);
                    }
                }),
            sdbus::registerProperty("ApiVersion").withGetter([] {
                return platform_linux::kApiVersion;
            }))
        .forInterface(sdbus::InterfaceName{platform_linux::kInterfaceName});
}

}  // namespace devmgr::daemon
```

Create `daemon/src/polkit_authority.hpp`:

```cpp
#pragma once
#include <string>

#include "devmgr/daemon/authority.hpp"

namespace devmgr::daemon {

// IAuthority over polkit's own D-Bus API (CheckAuthorization with
// AllowUserInteraction) — no libpolkit link. Opens a fresh short-lived
// system-bus connection per check: the daemon's main connection is busy
// dispatching the method call that triggered us, and sdbus-c++ v2 sync calls
// block their connection.
class PolkitAuthority final : public IAuthority {
   public:
    core::Result<bool> checkAuthorized(const CallerId& caller,
                                       const std::string& actionId) override;
};

}  // namespace devmgr::daemon
```

Create `daemon/src/polkit_authority.cpp`:

```cpp
#include "daemon/src/polkit_authority.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <tuple>

#include <sdbus-c++/sdbus-c++.h>

namespace devmgr::daemon {

core::Result<bool> PolkitAuthority::checkAuthorized(const CallerId& caller,
                                                    const std::string& actionId) {
    using Subject = sdbus::Struct<std::string, std::map<std::string, sdbus::Variant>>;
    using CheckResult = sdbus::Struct<bool, bool, std::map<std::string, std::string>>;
    try {
        auto connection = sdbus::createSystemBusConnection();
        auto proxy = sdbus::createProxy(
            std::move(connection), sdbus::ServiceName{"org.freedesktop.PolicyKit1"},
            sdbus::ObjectPath{"/org/freedesktop/PolicyKit1/Authority"});
        const Subject subject{"system-bus-name", {{"name", sdbus::Variant{caller}}}};
        const std::map<std::string, std::string> details;
        const std::uint32_t flags = 1;  // AllowUserInteraction — may block ~minutes
        CheckResult result;
        proxy->callMethod("CheckAuthorization")
            .onInterface(sdbus::InterfaceName{"org.freedesktop.PolicyKit1.Authority"})
            .withArguments(subject, actionId, details, flags, std::string{})
            .withTimeout(std::chrono::seconds(110))  // under the client's 120 s budget
            .storeResultsTo(result);
        return std::get<0>(result);
    } catch (const sdbus::Error& e) {
        return core::makeError(core::Error::Code::Io,
                               "polkit unavailable: " + std::string{e.getMessage()});
    }
}

}  // namespace devmgr::daemon
```

Create `daemon/src/main.cpp`:

```cpp
#include <cstdio>
#include <memory>
#include <string>

#include <sdbus-c++/sdbus-c++.h>
#include <spdlog/spdlog.h>

#include "daemon/src/manager_adaptor.hpp"
#include "daemon/src/polkit_authority.hpp"
#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/request_processor.hpp"
#include "devmgr/platform/linux/dbus_contract.hpp"
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#include "devmgr/platform/linux/sysfs_device_controller.hpp"
#include "devmgr/runtime/logging.hpp"

namespace {

struct Options {
    std::string bus = "system";
    std::string sysfsRoot = "/sys";
    std::string mountsPath = "/proc/self/mounts";
    std::string authority = "polkit";
    bool valid = true;
};

Options parse(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--bus" && hasValue) {
            opts.bus = argv[++i];
        } else if (arg == "--sysfs-root" && hasValue) {
            opts.sysfsRoot = argv[++i];
        } else if (arg == "--mounts-path" && hasValue) {
            opts.mountsPath = argv[++i];
        } else if (arg == "--authority" && hasValue) {
            opts.authority = argv[++i];
        } else {
            opts.valid = false;
        }
    }
    if (opts.bus != "system" && opts.bus != "session") opts.valid = false;
    if (opts.authority != "polkit" && opts.authority != "allow-all" &&
        opts.authority != "deny-all")
        opts.valid = false;
    return opts;
}

std::unique_ptr<devmgr::daemon::IAuthority> makeAuthority(const std::string& kind) {
    if (kind == "allow-all") return std::make_unique<devmgr::daemon::AllowAllAuthority>();
    if (kind == "deny-all") return std::make_unique<devmgr::daemon::DenyAllAuthority>();
    return std::make_unique<devmgr::daemon::PolkitAuthority>();
}

}  // namespace

int main(int argc, char** argv) {
    using namespace devmgr;
    const Options opts = parse(argc, argv);
    if (!opts.valid) {
        std::fprintf(stderr,
                     "usage: devmgrd [--bus system|session] [--sysfs-root PATH] "
                     "[--mounts-path PATH] [--authority polkit|allow-all|deny-all]\n");
        return 2;
    }
    devmgr::runtime::init();  // spdlog global setup — the repo's logging entry point
    if (opts.bus != "system" || opts.sysfsRoot != "/sys" ||
        opts.mountsPath != "/proc/self/mounts" || opts.authority != "polkit") {
        spdlog::warn(
            "running with NON-DEFAULT seams (test/dev mode): bus={} sysfs={} mounts={} "
            "authority={} — never use these in production",
            opts.bus, opts.sysfsRoot, opts.mountsPath, opts.authority);
    }
    try {
        auto connection =
            opts.bus == "session"
                ? sdbus::createSessionBusConnection(sdbus::ServiceName{platform_linux::kBusName})
                : sdbus::createSystemBusConnection(sdbus::ServiceName{platform_linux::kBusName});
        platform_linux::SysfsDeviceController controller(opts.sysfsRoot);
        platform_linux::LinuxCriticalityProber prober(opts.sysfsRoot, opts.mountsPath);
        auto authority = makeAuthority(opts.authority);
        daemon::RequestProcessor processor(controller, prober, *authority, opts.sysfsRoot);
        daemon::ManagerAdaptor adaptor(*connection, processor);
        spdlog::info("devmgrd serving {} on the {} bus", platform_linux::kBusName, opts.bus);
        connection->enterEventLoop();
    } catch (const std::exception& e) {
        spdlog::error("devmgrd failed: {}", e.what());
        return 1;
    }
    return 0;
}
```

Create `daemon/data/org.devmgr.Manager1.conf`:

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <!-- Phase 4: root owns the name; anyone may call. Authorization is
       polkit's job (org.devmgr.set-device-enabled), not the bus policy's. -->
  <policy user="root">
    <allow own="org.devmgr.Manager1"/>
  </policy>
  <policy context="default">
    <allow send_destination="org.devmgr.Manager1"/>
  </policy>
</busconfig>
```

Create `daemon/data/org.devmgr.policy`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE policyconfig PUBLIC "-//freedesktop//DTD PolicyKit Policy Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/PolicyKit/1.0/policyconfig.dtd">
<policyconfig>
  <vendor>cross-device-manager</vendor>
  <action id="org.devmgr.set-device-enabled">
    <description>Enable or disable a device</description>
    <message>Authentication is required to enable or disable a device</message>
    <defaults>
      <allow_any>auth_admin</allow_any>
      <allow_inactive>auth_admin</allow_inactive>
      <allow_active>auth_admin_keep</allow_active>
    </defaults>
  </action>
</policyconfig>
```

- [ ] **Step 6: Build + manual session-bus smoke (agent-runnable)**

Run: `cmake --preset linux-debug -DDEVMGR_WITH_SDBUS=ON && cmake --build --preset linux-debug`
Expected: `devmgrd` builds. Then:

```bash
mkdir -p /tmp/fakesys/devices/usb1/1-4 && echo 1 > /tmp/fakesys/devices/usb1/1-4/authorized
dbus-run-session -- sh -c '
  ./build/linux-debug/daemon/devmgrd --bus session --sysfs-root /tmp/fakesys \
      --mounts-path /dev/null --authority allow-all &
  sleep 0.5
  busctl --user call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 \
      SetDeviceEnabled sb /tmp/fakesys/devices/usb1/1-4 false
  cat /tmp/fakesys/devices/usb1/1-4/authorized
  kill %1'
```

Expected: `busctl` returns cleanly; the file prints `0`. (`--mounts-path /dev/null` = empty mounts = no storage facts.) Full suite still green: `ctest --test-dir build/linux-debug --output-on-failure`.

- [ ] **Step 7: Format check**

Run: `clang-format --dry-run --Werror platform/linux/include/devmgr/platform/linux/dbus_contract.hpp tests/unit/test_dbus_contract.cpp daemon/src/manager_adaptor.hpp daemon/src/manager_adaptor.cpp daemon/src/polkit_authority.hpp daemon/src/polkit_authority.cpp daemon/src/main.cpp`
Expected: no output.

- [ ] **Step 8: Hand off for commit**

Suggested message: `feat(daemon): devmgrd — sdbus-c++ v2 adaptor, polkit CheckAuthorization authority, D-Bus/polkit data files; sdbus CMake gating`

---

### Task 7: `DbusPrivilegedChannel` + IPC round-trip suite (`tests/ipc/`)

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/dbus_privileged_channel.hpp`
- Create: `platform/linux/src/dbus_privileged_channel.cpp` (sdbus leaf #2)
- Modify: `platform/linux/CMakeLists.txt`
- Create: `tests/ipc/CMakeLists.txt`, `tests/ipc/test_ipc_round_trip.cpp`
- Modify: `CMakeLists.txt` (root — `add_subdirectory(tests/ipc)` behind the flag)

**Interfaces:**
- Consumes: `pal::IPrivilegedChannel` (T1), `dbus_contract.hpp` (T6), `devmgrd` (T6).
- Produces (T8/T9 depend on): `platform_linux::DbusPrivilegedChannel` with `enum class Bus { System, Session }`, `explicit DbusPrivilegedChannel(Bus bus = Bus::System)`; compile definition `DEVMGR_HAS_SDBUS` propagated PUBLIC from `devmgr_pal_linux` when built with sdbus.

- [ ] **Step 1: Write the failing tests**

Create `tests/ipc/test_ipc_round_trip.cpp`:

```cpp
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "devmgr/core/models.hpp"
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using devmgr::platform_linux::DbusPrivilegedChannel;

namespace {

devmgr::core::Device deviceAt(const std::string& path) {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{"ipc-test"};
    d.sysfsPath = path;
    d.name = "ipc test device";
    return d;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

// Spawns devmgrd on the private session bus (the whole binary runs under
// dbus-run-session — see CMake add_test) against a fake sysfs tree.
class IpcRoundTripTest : public ::testing::Test {
   protected:
    fs::path root_;
    fs::path device_;
    pid_t daemonPid_ = -1;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-ipc-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        device_ = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device_);
        std::ofstream(device_ / "authorized") << "1";
        std::ofstream(root_ / "mounts") << "";  // no storage facts by default
    }

    void startDaemon(const char* authority) {
        daemonPid_ = ::fork();
        ASSERT_NE(daemonPid_, -1);
        if (daemonPid_ == 0) {
            ::execl(DEVMGRD_BIN, "devmgrd", "--bus", "session", "--sysfs-root",
                    root_.c_str(), "--mounts-path", (root_ / "mounts").c_str(),
                    "--authority", authority, static_cast<char*>(nullptr));
            ::_exit(127);
        }
        // Up when a bogus-path call stops failing with "helper unavailable".
        DbusPrivilegedChannel probe(DbusPrivilegedChannel::Bus::Session);
        for (int i = 0; i < 100; ++i) {
            auto r = probe.setDeviceEnabled(deviceAt("/nonexistent"), false);
            if (!r.has_value() && r.error().code == Error::Code::NotFound) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        FAIL() << "devmgrd did not come up on the session bus";
    }

    void TearDown() override {
        if (daemonPid_ > 0) {
            ::kill(daemonPid_, SIGTERM);
            int status = 0;
            ::waitpid(daemonPid_, &status, 0);
        }
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
};

}  // namespace

TEST_F(IpcRoundTripTest, DisableAndEnableFlipAuthorizedThroughTheBus) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto off = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_TRUE(off.has_value()) << off.error().message;
    EXPECT_EQ(readFile(device_ / "authorized"), "0");
    auto on = channel.setDeviceEnabled(deviceAt(device_.string()), true);
    ASSERT_TRUE(on.has_value()) << on.error().message;
    EXPECT_EQ(readFile(device_ / "authorized"), "1");
}

TEST_F(IpcRoundTripTest, GuardRefusalArrivesAsConflictWithReason) {
    // Fake tree where the target USB device hosts the root filesystem's disk.
    const fs::path block = device_ / "1-4:1.0/host7/block/sdb";
    fs::create_directories(block);
    fs::create_directories(root_ / "class/block");
    fs::create_directory_symlink(block, root_ / "class/block/sdb");
    fs::create_directories(root_ / "dev");
    std::ofstream(root_ / "dev/sdb") << "";
    std::ofstream(root_ / "mounts") << (root_ / "dev/sdb").string() + " / ext4 rw 0 0\n";

    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Conflict);
    EXPECT_EQ(r.error().message, "backs the root filesystem");
    EXPECT_EQ(readFile(device_ / "authorized"), "1");  // untouched
}

TEST_F(IpcRoundTripTest, DeniedAuthorityArrivesAsPermission) {
    startDaemon("deny-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Permission);
}

TEST_F(IpcRoundTripTest, MissingDeviceArrivesAsNotFound) {
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt((root_ / "devices/ghost").string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(IpcRoundTripTest, DeviceWithoutAuthorizedAttrArrivesAsUnsupported) {
    const fs::path pci = root_ / "devices/pci0000:00/0000:00:02.0";
    fs::create_directories(pci);
    startDaemon("allow-all");
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(pci.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}

TEST_F(IpcRoundTripTest, AbsentDaemonIsHelperUnavailableIo) {
    DbusPrivilegedChannel channel(DbusPrivilegedChannel::Bus::Session);
    auto r = channel.setDeviceEnabled(deviceAt(device_.string()), false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_EQ(r.error().message, "helper devmgrd is not available");
}
```

Create `tests/ipc/CMakeLists.txt`:

```cmake
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_ipc_tests test_ipc_round_trip.cpp)
target_link_libraries(devmgr_ipc_tests
    PRIVATE devmgr_core devmgr_pal_linux GTest::gtest GTest::gtest_main)
target_compile_definitions(devmgr_ipc_tests PRIVATE DEVMGRD_BIN="$<TARGET_FILE:devmgrd>")
target_compile_features(devmgr_ipc_tests PRIVATE cxx_std_20)

# One private session bus for daemon + client; nothing touches the real buses.
add_test(NAME devmgr_ipc COMMAND dbus-run-session -- $<TARGET_FILE:devmgr_ipc_tests>)
```

Root `CMakeLists.txt`, after `add_subdirectory(tests)`:

```cmake
if(DEVMGR_WITH_SDBUS)
    add_subdirectory(tests/ipc)
endif()
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --preset linux-debug`
Expected: CMake generate error — missing `dbus_privileged_channel.hpp` / unknown sources.

- [ ] **Step 3: Implement**

Create `platform/linux/include/devmgr/platform/linux/dbus_privileged_channel.hpp`:

```cpp
#pragma once
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// IPrivilegedChannel over the D-Bus system bus → devmgrd. sdbus-c++ LEAF
// FILE #2 (the .cpp; this header is sdbus-free). Each call opens a fresh
// short-lived connection+proxy (sdbus-c++ v2 sync calls block their
// connection; mutations are rare). Blocking with a 120 s timeout — the
// interactive-polkit budget — so callers must run it on a worker thread.
// Bus::Session exists for the private-bus integration tests only.
class DbusPrivilegedChannel final : public pal::IPrivilegedChannel {
   public:
    enum class Bus { System, Session };
    explicit DbusPrivilegedChannel(Bus bus = Bus::System);
    core::Result<void> setDeviceEnabled(const core::Device& device, bool enabled) override;

   private:
    Bus bus_;
};

}  // namespace devmgr::platform_linux
```

Create `platform/linux/src/dbus_privileged_channel.cpp`:

```cpp
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"

#include <chrono>
#include <string>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/dbus_contract.hpp"

namespace devmgr::platform_linux {

DbusPrivilegedChannel::DbusPrivilegedChannel(Bus bus) : bus_(bus) {}

core::Result<void> DbusPrivilegedChannel::setDeviceEnabled(const core::Device& device,
                                                           bool enabled) {
    try {
        auto connection = bus_ == Bus::Session ? sdbus::createSessionBusConnection()
                                               : sdbus::createSystemBusConnection();
        auto proxy = sdbus::createProxy(std::move(connection), sdbus::ServiceName{kBusName},
                                        sdbus::ObjectPath{kObjectPath});
        proxy->callMethod("SetDeviceEnabled")
            .onInterface(sdbus::InterfaceName{kInterfaceName})
            .withArguments(device.sysfsPath, enabled)
            .withTimeout(std::chrono::minutes(2));
        return {};
    } catch (const sdbus::Error& e) {
        return tl::unexpected(
            coreErrorFor(std::string{e.getName()}, std::string{e.getMessage()}));
    }
}

}  // namespace devmgr::platform_linux
```

`platform/linux/CMakeLists.txt`, append:

```cmake
if(DEVMGR_WITH_SDBUS)
    target_sources(devmgr_pal_linux PRIVATE src/dbus_privileged_channel.cpp)
    target_link_libraries(devmgr_pal_linux PRIVATE SDBusCpp::sdbus-c++)
    # Lets composition roots (tui/gui) wire the channel only when it exists.
    target_compile_definitions(devmgr_pal_linux PUBLIC DEVMGR_HAS_SDBUS)
endif()
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass including `devmgr_ipc` (6 round-trip tests on the private bus).

- [ ] **Step 5: Format check**

Run: `clang-format --dry-run --Werror platform/linux/include/devmgr/platform/linux/dbus_privileged_channel.hpp platform/linux/src/dbus_privileged_channel.cpp tests/ipc/test_ipc_round_trip.cpp`
Expected: no output.

- [ ] **Step 6: Hand off for commit**

Suggested message: `feat(platform): DbusPrivilegedChannel client + private-session-bus IPC round-trip suite (tests/ipc)`

---

### Task 8: TUI wiring — `e` toggle + confirm + auto-refresh

**Files:**
- Modify: `tui/src/tui_app.cpp`

**Interfaces:**
- Consumes: `facade.setDeviceEnabled/canDisable` (T5), `DbusPrivilegedChannel`/`DEVMGR_HAS_SDBUS` (T7), `LinuxCriticalityProber` (T3), StatusLineVM task messages (T5).
- Produces: no new API. Behavior: `e` on a device row → advisory guard check → inline `(y/n)` confirm in the status line → mutation future joins `pending`; every successful mutation triggers a refresh; results/refusals surface in the status line.

- [ ] **Step 1: Implement (verification is the offscreen/manual smoke — FTXUI interaction has no unit harness, same as Phases 1–3)**

In `tui/src/tui_app.cpp`:

1. Add includes after the `udev_hotplug_monitor.hpp` include:

```cpp
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#ifdef DEVMGR_HAS_SDBUS
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"
#endif
```

2. Replace the facade construction block (`app::ApplicationFacade facade(...)`) with:

```cpp
    platform_linux::LinuxCriticalityProber prober;  // advisory guard facts
#ifdef DEVMGR_HAS_SDBUS
    platform_linux::DbusPrivilegedChannel channel;  // system bus → devmgrd
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, &channel, &prober);
#else
    app::ApplicationFacade facade(enumerator, scheduler, bus, service, nullptr, &prober);
#endif
```

3. After the `std::vector<std::future<void>> pending;` declaration, add the confirm state and the auto-refresh subscription:

```cpp
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
```

   While here, replace the inline `std::erase_if(...)` block inside the existing `'r'` handler with a `prunePending();` call — same behavior, one definition.

4. In the header line of the renderer, change the hint text:

```cpp
                           text(" Devices (r=refresh  e=enable/disable  q=quit) ") | bold,
```

5. In the status line of the renderer, show the confirm prompt while armed:

```cpp
                   text(" " + (confirmToggle ? confirmToggle->prompt : statusVm.text()) + " ") |
                       inverted,
```

6. In the `CatchEvent` handler, insert BEFORE the `'q'` branch (the confirm gate must run first and swallow keys while armed):

```cpp
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
                    bus.publish(core::TaskCompletedEvent{
                        .taskId = "guard", .ok = false,
                        .message = "cannot disable: " + verdict.reason});
                    return true;
                }
            }
            confirmToggle = PendingToggle{
                *id, enable,
                std::string(enable ? "enable " : "disable ") + device->name + "? (y/n)"};
            return true;
        }
```

7. `refreshOnTaskDone` is a `runtime::Subscription` local declared after the VMs — it unsubscribes (reverse declaration order) before the VMs/`pending` unwind, and the existing `drainPending` calls already cover the mutation futures. Dispatcher posts still queued at exit are dropped by FTXUI's dispatcher — documented drop-after-teardown semantics, unchanged.

- [ ] **Step 2: Build + full suite**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: builds clean, all tests pass (no test-count change — TUI has no unit harness).

- [ ] **Step 3: Offscreen sanity**

Run: `./build/linux-debug/tui/devmgr-tui </dev/null || true` — must not crash on startup/teardown (exits immediately without a TTY loop, mirroring prior phases' sanity check).

- [ ] **Step 4: Format check**

Run: `clang-format --dry-run --Werror tui/src/tui_app.cpp`
Expected: no output.

- [ ] **Step 5: Hand off for commit**

Suggested message: `feat(tui): 'e' enable/disable with inline confirm, advisory guard refusals, auto-refresh on mutation`

---

### Task 9: GUI wiring — toggle action + confirm + auto-refresh

**Files:**
- Modify: `gui/src/main_window.hpp`, `gui/src/main_window.cpp`
- Modify: `gui/src/gui_app.cpp`
- Modify: `gui/tests/test_main_window.cpp`

**Interfaces:**
- Consumes: `facade.setDeviceEnabled/canDisable/findById` (T5), `DEVMGR_HAS_SDBUS`/channel (T7), prober (T3).
- Produces: `MainWindow` ctor becomes
  `MainWindow(app::ApplicationFacade& facade, app::DeviceListVM& listVm, app::DeviceDetailVM& detailVm, app::StatusLineVM& statusVm, QtUiDispatcher& dispatcher, std::function<void()> onRefresh, std::function<void(const core::DeviceId&, bool)> onSetEnabled, std::function<bool(const QString&)> confirm = {}, QWidget* parent = nullptr)`
  — `confirm` null → `QMessageBox::question`; tests inject a recorder. New test accessor `QAction* toggleAction() const`.

- [ ] **Step 1: Write the failing tests**

In `gui/tests/test_main_window.cpp`, update the `Fixture` (facade + recorder plumbing) — replace `refreshCalls`/`makeWindow`:

```cpp
    int refreshCalls = 0;
    std::vector<std::pair<std::string, bool>> setEnabledCalls;
    bool confirmAnswer = true;

    gui::MainWindow makeWindow() {
        return gui::MainWindow(
            facade, listVm, detailVm, statusVm, dispatcher, [this] { ++refreshCalls; },
            [this](const core::DeviceId& id, bool enable) {
                setEnabledCalls.emplace_back(id.value, enable);
            },
            [this](const QString&) { return confirmAnswer; });
    }
```

Append the new tests:

```cpp
TEST(MainWindowTest, ToggleActionDisabledWithoutSelectionEnabledOnDeviceRow) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    EXPECT_FALSE(window.toggleAction()->isEnabled());

    f.refreshAndPump();
    const int row = f.firstDeviceRow();
    ASSERT_GE(row, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(row, 0));
    EXPECT_TRUE(window.toggleAction()->isEnabled());
    EXPECT_EQ(window.toggleAction()->text(), QStringLiteral("Disable"));
}

TEST(MainWindowTest, ConfirmedTriggerInvokesOnSetEnabled) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    window.listView()->setCurrentIndex(
        window.listView()->model()->index(f.firstDeviceRow(), 0));

    window.toggleAction()->trigger();
    ASSERT_EQ(f.setEnabledCalls.size(), 1u);
    EXPECT_EQ(f.setEnabledCalls[0].first, "u1");
    EXPECT_FALSE(f.setEnabledCalls[0].second);  // Active device → disable
}

TEST(MainWindowTest, DeclinedConfirmSendsNothing) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    window.listView()->setCurrentIndex(
        window.listView()->model()->index(f.firstDeviceRow(), 0));

    f.confirmAnswer = false;
    window.toggleAction()->trigger();
    EXPECT_TRUE(f.setEnabledCalls.empty());
}

TEST(MainWindowTest, DisabledDeviceOffersEnableWithoutGuardCheck) {
    Fixture f;
    auto d = dev("u1", core::BusType::Usb, "Webcam");
    d.status = core::DeviceStatus::Disabled;
    f.pal.seedDevice(d);
    auto window = f.makeWindow();
    f.refreshAndPump();
    window.listView()->setCurrentIndex(
        window.listView()->model()->index(f.firstDeviceRow(), 0));

    EXPECT_EQ(window.toggleAction()->text(), QStringLiteral("Enable"));
    window.toggleAction()->trigger();
    ASSERT_EQ(f.setEnabledCalls.size(), 1u);
    EXPECT_TRUE(f.setEnabledCalls[0].second);
}
```

(The advisory-grey-out path — `canDisable` refusing — is covered by the facade unit tests of T5; the window consults the same facade method, verified by reading `toggleAction()->toolTip()` in the smoke. A GUI test with a critical fake device would need a prober fake injected into the fixture's facade: add it only if it stays a one-liner — `app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, &prober};` with a `test::FakeCriticalityProber prober;` member — and assert the action is disabled with the reason tooltip after selecting the row.)

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `MainWindow` has no matching constructor / no member `toggleAction`.

- [ ] **Step 3: Implement**

`gui/src/main_window.hpp`:
- Add includes: `#include "devmgr/app/application_facade.hpp"` and forward-declare `class QAction;` next to the other forward declarations.
- Replace the constructor declaration with the T9 signature (Interfaces block above).
- Add accessor + members:

```cpp
    QAction* toggleAction() const { return toggleAction_; }
```

```cpp
    void updateToggleAction();

    app::ApplicationFacade& facade_;
    std::function<void(const core::DeviceId&, bool)> onSetEnabled_;
    std::function<bool(const QString&)> confirm_;
    QAction* toggleAction_ = nullptr;
```

`gui/src/main_window.cpp`:
- Add `#include <QMessageBox>` and `#include "devmgr/core/models.hpp"`.
- Update the constructor signature and init list (`facade_(facade)`, `onSetEnabled_(std::move(onSetEnabled))`, `confirm_(std::move(confirm))`).
- After the Refresh action creation (keep Refresh FIRST — the existing toolbar test indexes `actions.first()`):

```cpp
    toggleAction_ = toolbar->addAction(QStringLiteral("Disable"));
    toggleAction_->setEnabled(false);
    connect(toggleAction_, &QAction::triggered, this, [this] {
        const auto id = listVm_.selectedDeviceId();
        const auto device = id ? facade_.findById(*id) : std::nullopt;
        if (!device) return;
        const bool enable = device->status == core::DeviceStatus::Disabled;
        const QString prompt = QStringLiteral("%1 %2?").arg(
            enable ? QStringLiteral("Enable") : QStringLiteral("Disable"),
            QString::fromStdString(device->name));
        const bool go = confirm_ ? confirm_(prompt)
                                 : QMessageBox::question(this, QStringLiteral("Confirm"),
                                                         prompt) == QMessageBox::Yes;
        if (go) onSetEnabled_(*id, enable);
    });
```

  Then, after the existing `listView_->setEditTriggers(...)` line, make the same action serve as the list's context menu (parity with the TUI's `e` on the focused list):

```cpp
    // Same action doubles as the list's context menu.
    listView_->addAction(toggleAction_);
    listView_->setContextMenuPolicy(Qt::ActionsContextMenu);
```

- In the `currentChanged` lambda, after `updateDetailPane();` add `updateToggleAction();`. In the `modelReset` lambda, after `updateDetailPane();` add `updateToggleAction();`.
- Implement:

```cpp
void MainWindow::updateToggleAction() {
    const auto id = listVm_.selectedDeviceId();
    const auto device = id ? facade_.findById(*id) : std::nullopt;
    if (!device) {
        toggleAction_->setEnabled(false);
        toggleAction_->setText(QStringLiteral("Disable"));
        toggleAction_->setToolTip({});
        return;
    }
    const bool enable = device->status == core::DeviceStatus::Disabled;
    toggleAction_->setText(enable ? QStringLiteral("Enable") : QStringLiteral("Disable"));
    if (!enable) {
        // Advisory only — devmgrd re-checks authoritatively on every request.
        const auto verdict = facade_.canDisable(*id);
        toggleAction_->setEnabled(verdict.allowed);
        toggleAction_->setToolTip(verdict.allowed
                                      ? QString{}
                                      : QString::fromStdString("cannot disable: " +
                                                               verdict.reason));
        return;
    }
    toggleAction_->setEnabled(true);
    toggleAction_->setToolTip({});
}
```

`gui/src/gui_app.cpp`:
- Includes (mirror the TUI):

```cpp
#include "devmgr/platform/linux/linux_criticality_prober.hpp"
#ifdef DEVMGR_HAS_SDBUS
#include "devmgr/platform/linux/dbus_privileged_channel.hpp"
#endif
```

- Facade construction (same `#ifdef` block as the TUI, T8 step 2).
- Replace the `MainWindow window(...)` construction with:

```cpp
    auto pruneAndPush = [&](std::future<void> f) {
        std::erase_if(pending, [](const std::future<void>& g) {
            return g.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
        pending.push_back(std::move(f));
    };
    MainWindow window(
        facade, listVm, detailVm, statusVm, dispatcher,
        [&] { pruneAndPush(facade.refresh()); },
        [&](const core::DeviceId& id, bool enable) {
            pruneAndPush(facade.setDeviceEnabled(id, enable));
        });
```

- After the `pending` declaration, the auto-refresh subscription (same rationale comment as the TUI):

```cpp
    auto refreshOnTaskDone =
        bus.subscribe<core::TaskCompletedEvent>([&](const core::TaskCompletedEvent& e) {
            if (!e.ok) return;
            dispatcher.post([&] { pruneAndPush(facade.refresh()); });
        });
```

  Note: `pruneAndPush` must be declared before both the subscription and the window — move it up right after `pending`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (+4 window tests; existing 5 window tests updated fixture compiles).

- [ ] **Step 5: Offscreen self-test**

Run: `QT_QPA_PLATFORM=offscreen ./build/linux-debug/gui/devmgr-gui --self-test`
Expected: prints `self-test rows: <N>` (N > 0), exit 0.

- [ ] **Step 6: Format check**

Run: `clang-format --dry-run --Werror gui/src/main_window.hpp gui/src/main_window.cpp gui/src/gui_app.cpp gui/tests/test_main_window.cpp`
Expected: no output.

- [ ] **Step 7: Hand off for commit**

Suggested message: `feat(gui): Enable/Disable toolbar+context action with confirm, advisory guard tooltip, auto-refresh on mutation`

---

### Task 10: Dockerfile sdbus build, CI guards, README, VM script

**Files:**
- Modify: `Dockerfile`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Create: `test/vm/phase4-smoke.sh`

- [ ] **Step 1: Dockerfile — pinned sdbus-c++ v2.3.1 + dbus tools**

Add `libsystemd-dev dbus` to the apt list, then insert a build layer after the vcpkg clone:

```dockerfile
# sdbus-c++ v2 (pinned to match the dev host's portage 2.3.1): Ubuntu 24.04
# ships 1.4 and v2.0 was a breaking API rewrite — see the Phase 4 spec.
RUN git clone --depth 1 --branch v2.3.1 https://github.com/Kistler-Group/sdbus-cpp.git /tmp/sdbus-cpp \
    && cmake -S /tmp/sdbus-cpp -B /tmp/sdbus-cpp/build \
         -DCMAKE_BUILD_TYPE=Release -DSDBUSCPP_BUILD_CODEGEN=OFF \
    && cmake --build /tmp/sdbus-cpp/build -j \
    && cmake --install /tmp/sdbus-cpp/build \
    && rm -rf /tmp/sdbus-cpp
```

(`dbus` provides `dbus-run-session` for the `devmgr_ipc` ctest inside the container.)

- [ ] **Step 2: CI workflow**

In `.github/workflows/ci.yml`:
1. Format step — add `daemon` to the `find` roots: `find core tests app platform tui gui daemon ...`.
2. Toolkit purity — add `daemon` to the grep paths: `core app platform daemon`.
3. New step after the toolkit guard:

```yaml
      - name: sdbus purity guard (sdbus-c++ only in daemon/ + the channel leaf)
        # The Phase 4 quarantine: sdbus types must never leak into the shared
        # layers. Allowed: daemon/src/* and the one client leaf file.
        run: |
          ! grep -rn --include='*.hpp' --include='*.cpp' -E '#include\s*[<"]sdbus' \
              core app gui tui tests platform \
              | grep -v '^platform/linux/src/dbus_privileged_channel.cpp:'
```

4. clang-tidy step — extend the file list: `... gui/src/*.cpp daemon/src/*.cpp`.

- [ ] **Step 3: Create `test/vm/phase4-smoke.sh` (user-run, VM ONLY)**

```bash
#!/usr/bin/env bash
# Phase 4 dangerous E2E — run INSIDE the disposable VM as root, NEVER on a host.
# Usage: phase4-smoke.sh /sys/devices/.../<usb-device>   (a QEMU-attached spare,
# e.g. usb-storage; root is implicitly polkit-authorized so no agent is needed)
set -euo pipefail
DEV=${1:?usage: phase4-smoke.sh <sysfs path of a spare USB device>}
[ -f "$DEV/authorized" ] || { echo "no authorized attr at $DEV"; exit 1; }

install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/
install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/
./build/linux-debug/daemon/devmgrd &
DPID=$!
trap 'kill "$DPID" 2>/dev/null || true' EXIT
sleep 1

busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 \
    SetDeviceEnabled sb "$DEV" false
[ "$(cat "$DEV/authorized")" = "0" ] || { echo "disable did not stick"; exit 1; }

busctl call org.devmgr.Manager1 /org/devmgr/Manager1 org.devmgr.Manager1 \
    SetDeviceEnabled sb "$DEV" true
[ "$(cat "$DEV/authorized")" = "1" ] || { echo "enable did not stick"; exit 1; }

echo "PHASE4 VM SMOKE OK"
```

Run: `chmod +x test/vm/phase4-smoke.sh`

- [ ] **Step 4: README — Phase 4 section**

Append a `## Enable/Disable (Phase 4)` section documenting: what it does (USB `authorized` only), the host manual-run steps (install the two data files, `sudo ./build/linux-debug/daemon/devmgrd`, run either UI in a graphical session), the auth posture (`auth_admin_keep`; a desktop polkit agent is required — pure-console auth is unsupported this phase), and the guard behavior (refuses root/boot-backing and sole-input devices with a reason).

- [ ] **Step 5: Container run (user or agent with Docker access)**

Run: `podman-compose -f test/docker-compose.yml build unit && podman-compose -f test/docker-compose.yml run --rm unit`
(Gotcha from T6/Phase 3: `run` reuses a stale image — always `build` first after Dockerfile/source changes.)
Expected: full suite green in the container, including `devmgr_integration` (now +1 mapper test) and `devmgr_ipc`.

- [ ] **Step 6: Format + hand off for commit**

Run: `clang-format --dry-run --Werror` on any touched C++ files (none expected in this task).
Suggested message: `ci: sdbus-c++ v2.3.1 in container, sdbus purity guard, daemon in format/tidy; Phase 4 docs + VM smoke script`

---

## Manual smoke (USER, real host — PHASE EXIT GATE)

Prereqs: `sudo emerge dev-cpp/sdbus-c++` done (T6); build current.

1. `sudo install -m644 daemon/data/org.devmgr.Manager1.conf /etc/dbus-1/system.d/`
2. `sudo install -m644 daemon/data/org.devmgr.policy /usr/share/polkit-1/actions/`
3. `sudo ./build/linux-debug/daemon/devmgrd` (foreground; OpenRC — no unit; watch its log lines)
4. In a **graphical session**, plug in a spare USB device (mouse you can spare, webcam, stick — NOT your only keyboard/pointer, NOT a root-backing disk).
5. `./build/linux-debug/tui/devmgr-tui`: select the device → `e` → `y` → polkit prompt appears → authenticate → status line shows "Disabled <name>" → device shows **Disabled** after the auto-refresh (deauthorized: its interfaces vanish). `e` → `y` again re-enables **without a new prompt** (auth_admin_keep caches ~5 min).
6. `./build/linux-debug/gui/devmgr-gui`: same round trip via the toolbar/context **Disable/Enable** action with the QMessageBox confirm.
7. Guard checks: select your only keyboard → TUI `e` shows "cannot disable: would disable the only keyboard" (no prompt, nothing sent); GUI greys the action with the same tooltip reason. Select a device backing `/` (e.g. the NVMe controller) → same with "backs the root filesystem".
8. Negative path: stop devmgrd, try a disable → "helper devmgrd is not available" on the status line; UI stays healthy.
9. VM dangerous script: snapshot the disposable VM → `test/vm/phase4-smoke.sh <path>` → `PHASE4 VM SMOKE OK` → revert snapshot.

Then: finishing-a-development-branch skill (merge/PR decision) for `feature/phase4`.
