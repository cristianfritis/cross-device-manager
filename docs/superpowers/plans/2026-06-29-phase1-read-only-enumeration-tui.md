# Phase 1 — Read-only Enumeration + FTXUI TUI — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Launch `devmgr-tui` on a Linux machine and see the machine's **real** devices — grouped/sorted by bus, searchable, with a detail pane, manual refresh (`r`), and mouse support.

**Architecture:** A real `UdevDeviceEnumerator` (libudev, isolated in a Linux-only `devmgr_pal_linux` lib, libudev linked PRIVATE) produces `Result<vector<Device>>`. `ApplicationFacade::refresh()` runs it on the existing `TaskScheduler` (UI thread never blocks), hands the snapshot to `DeviceService`, which diffs by `DeviceId` and publishes `DeviceAdded/Removed/Changed` on the existing `EventBus`. ViewModels marshal every UI mutation through a pure `IUiDispatcher`. The FTXUI frontend supplies the concrete `FtxuiUiDispatcher`. `devmgr_core` stays OS- and UI-agnostic; the app layer is FakePal-unit-tested; the udev path is umockdev-tested in the container.

**Tech Stack:** C++20, CMake + vcpkg, GoogleTest, FTXUI 6.1.9 (vcpkg), libudev v260 (system, pkg-config `libudev`), umockdev (system, pkg-config `umockdev-1.0`), spdlog, tl::expected.

## Global Constraints

Every task's requirements implicitly include this section.

- **C++20**, `CMAKE_CXX_EXTENSIONS OFF`, CMake ≥ 3.21, build via `cmake --preset linux-debug` (build dir `build/linux-debug`). `VCPKG_ROOT=/home/cfritis/dev/vcpkg`.
- **`devmgr_core` stays OS-agnostic and UI-agnostic** — it must never include `<libudev.h>`, any FTXUI header, or link a native/UI lib.
- **libudev is linked `PRIVATE`** in `devmgr_pal_linux` only. **`devmgr_tests` (unit suite) links NO native deps** (no libudev/umockdev/ftxui) — it must build and run on a bare host.
- **Linux platform namespace is `devmgr::platform_linux`** (avoids GCC's predefined `linux` macro); headers under `platform/linux/include/devmgr/platform/linux/`.
- **Stable `DeviceId` = FNV-1a-64** over `subsystem '\x1f' syspath '\x1f' vendor ':' product '\x1f' serial`, formatted `dev-%016llx`. Never `std::hash<std::string>` (not stable across processes).
- **Enumeration fault isolation:** whole-context failure → `makeError(Io)`; a single failed device → one synthetic `Device{status=Error, errorNote=…}`, never abort the scan.
- **All EventBus handlers must assume they run on a worker thread.** Any UI/VM-state mutation MUST go through `IUiDispatcher::post(...)`.
- **umockdev integration tests are gated** (`DEVMGR_BUILD_INTEGRATION_TESTS`, auto-ON only when `umockdev-1.0` is found) and run only inside the container under `umockdev-wrapper`. Never force-enable on a bare host.
- **clang-format clean per task.** After writing code, run:
  `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
  `.clang-format` keeps in-class one-liners inline but expands namespace-scope one-liners — write free-function one-liners multi-line.
- **COMMIT POLICY (critical):** This environment **denies `git add`/`git commit`** for the agent. **Do NOT run git.** Leave changes uncommitted; the final step of each task **hands the user a ready commit command** to run themselves. The user commits before the next task starts, keeping each task's working-tree diff clean.
- **Verification reality:** the agent CAN build + run the unit suite. It CANNOT run the umockdev integration test (no umockdev on host, no Docker daemon in-agent) or the manual TUI smoke (no real hardware/TTY) — those are the **user's** to run (container via rootless `podman-compose`; TUI on a real Linux box).

---

## File Structure

**New target `devmgr_pal_linux`** (Linux-only static lib; the only place libudev is linked):
- `platform/linux/CMakeLists.txt` — defines the lib, `PkgConfig::UDEV` PRIVATE.
- `platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp` — **header-only, NO `<libudev.h>`** pure helpers (`fnv1a64`, `stableId` from parts, `busFor`, `strip0x`, `firstNonEmpty`). Unit-testable without libudev.
- `platform/linux/include/devmgr/platform/linux/udev_device_enumerator.hpp` — `UdevDeviceEnumerator : pal::IDeviceEnumerator`.
- `platform/linux/src/udev_device_enumerator.cpp` — the libudev implementation (`mapDevice`, RAII deleters).

**New target `devmgr_app`** (OS/UI-agnostic static lib):
- `app/CMakeLists.txt`
- `app/include/devmgr/app/ui_dispatcher.hpp` — pure `IUiDispatcher` interface.
- `app/include/devmgr/app/device_service.hpp` + `app/src/device_service.cpp` — model + reconcile.
- `app/include/devmgr/app/application_facade.hpp` + `app/src/application_facade.cpp` — read API.
- `app/include/devmgr/app/device_list_vm.hpp` + `app/src/device_list_vm.cpp` — list rows/filter/grouping/selection.
- `app/include/devmgr/app/device_detail_vm.hpp` + `app/src/device_detail_vm.cpp` — detail lines.

**New target `devmgr-tui`** (executable, Linux-only):
- `tui/CMakeLists.txt`
- `tui/src/ftxui_ui_dispatcher.hpp` + `tui/src/ftxui_ui_dispatcher.cpp` — `IUiDispatcher` over FTXUI.
- `tui/src/tui_app.hpp` + `tui/src/tui_app.cpp` — component tree + key/mouse wiring.
- `tui/src/main.cpp` — composition root.

**Modified:**
- `core/include/devmgr/core/models.hpp` — add defaulted `Device` equality.
- `vcpkg.json` — add `"ftxui"`.
- `CMakeLists.txt` (root) — wire new subdirs + integration gate.
- `tests/CMakeLists.txt` — link `devmgr_app`, add new unit test sources + platform include path.
- `.github/workflows/ci.yml` — extend format + clang-tidy globs.

**New test files:**
- `tests/fakes/inline_ui_dispatcher.hpp` — synchronous `IUiDispatcher` double.
- `tests/unit/test_udev_field_mapping.cpp` (Task 2), `test_device_service.cpp` (Task 3), `test_application_facade.cpp` (Task 4), `test_device_list_vm.cpp` + `test_device_detail_vm.cpp` (Task 5).
- `tests/integration/CMakeLists.txt`, `tests/integration/test_udev_enumerator.cpp` (Task 2).

---

## Task 1: Build graph + dependency wiring

**Files:**
- Modify: `vcpkg.json`
- Modify: `CMakeLists.txt`
- Create: `platform/linux/CMakeLists.txt`, `platform/linux/include/devmgr/platform/linux/udev_device_enumerator.hpp`, `platform/linux/src/udev_device_enumerator.cpp`
- Create: `app/CMakeLists.txt`, `app/include/devmgr/app/.keep` placeholder TU `app/src/placeholder.cpp`
- Create: `tui/CMakeLists.txt`, `tui/src/main.cpp`
- Create: `tests/integration/CMakeLists.txt`, `tests/integration/test_udev_enumerator.cpp` (stub)
- Modify: `tests/CMakeLists.txt`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: existing `devmgr_core`, `pal::IDeviceEnumerator`, `core::makeError`.
- Produces: target `devmgr_pal_linux` exposing class `devmgr::platform_linux::UdevDeviceEnumerator` with `core::Result<std::vector<core::Device>> enumerate() override;` (stubbed here); empty `devmgr_app` lib; runnable `devmgr-tui` stub; gated `devmgr_integration_tests` target; CMake option `DEVMGR_BUILD_INTEGRATION_TESTS`.

- [ ] **Step 1: Add ftxui to the vcpkg manifest**

`vcpkg.json`:
```json
{
  "name": "cross-device-manager",
  "version": "0.1.0",
  "dependencies": ["gtest", "spdlog", "nlohmann-json", "tl-expected", "ftxui"]
}
```

- [ ] **Step 2: Create the `devmgr_pal_linux` stub**

`platform/linux/include/devmgr/platform/linux/udev_device_enumerator.hpp`:
```cpp
#pragma once
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::platform_linux {

// Real libudev-backed enumerator. libudev is an implementation detail (this
// header pulls NO <libudev.h>), so consumers stay free of the native dep.
class UdevDeviceEnumerator final : public pal::IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override;
};

}  // namespace devmgr::platform_linux
```

`platform/linux/src/udev_device_enumerator.cpp` (stub for Task 1; Task 2 replaces the body):
```cpp
#include "devmgr/platform/linux/udev_device_enumerator.hpp"

namespace devmgr::platform_linux {

core::Result<std::vector<core::Device>> UdevDeviceEnumerator::enumerate() {
    return core::makeError(core::Error::Code::Unsupported, "not implemented yet");
}

}  // namespace devmgr::platform_linux
```

`platform/linux/CMakeLists.txt`:
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED IMPORTED_TARGET libudev)   # -> PkgConfig::UDEV

add_library(devmgr_pal_linux STATIC src/udev_device_enumerator.cpp)
target_include_directories(devmgr_pal_linux PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgr_pal_linux
    PUBLIC devmgr_core
    PRIVATE PkgConfig::UDEV)
target_compile_features(devmgr_pal_linux PUBLIC cxx_std_20)
```

- [ ] **Step 3: Create the empty `devmgr_app` lib**

`app/src/placeholder.cpp`:
```cpp
// Placeholder TU so devmgr_app links before its real sources land (Tasks 3-5).
namespace devmgr::app { }
```

`app/CMakeLists.txt`:
```cmake
add_library(devmgr_app STATIC src/placeholder.cpp)
target_include_directories(devmgr_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgr_app PUBLIC devmgr_core)   # NO ftxui, NO libudev
target_compile_features(devmgr_app PUBLIC cxx_std_20)
```

- [ ] **Step 4: Create the `devmgr-tui` stub**

`tui/src/main.cpp`:
```cpp
#include <ftxui/component/component.hpp>          // Renderer
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

int main() {
    using namespace ftxui;
    auto screen = ScreenInteractive::Fullscreen();
    auto root = Renderer([] { return text("devmgr-tui (scaffold)") | border; });
    screen.Loop(root);
    return 0;
}
```

`tui/CMakeLists.txt`:
```cmake
find_package(ftxui CONFIG REQUIRED)

add_executable(devmgr-tui src/main.cpp)
target_link_libraries(devmgr-tui
    PRIVATE devmgr_app devmgr_core devmgr_pal_linux
            ftxui::component ftxui::dom ftxui::screen)
target_compile_features(devmgr-tui PRIVATE cxx_std_20)
```

- [ ] **Step 5: Create the gated integration target (stub)**

`tests/integration/test_udev_enumerator.cpp`:
```cpp
#include <gtest/gtest.h>

// Real udev + umockdev assertions land in Task 2. This placeholder proves the
// gated target builds and runs under umockdev-wrapper inside the container.
TEST(UdevEnumeratorIntegration, HarnessBuilds) {
    SUCCEED();
}
```

`tests/integration/CMakeLists.txt`:
```cmake
find_package(GTest CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UMOCKDEV REQUIRED IMPORTED_TARGET umockdev-1.0)   # -> PkgConfig::UMOCKDEV

add_executable(devmgr_integration_tests test_udev_enumerator.cpp)
target_link_libraries(devmgr_integration_tests
    PRIVATE devmgr_pal_linux devmgr_core
            PkgConfig::UMOCKDEV
            GTest::gtest GTest::gtest_main)
target_compile_features(devmgr_integration_tests PRIVATE cxx_std_20)

# Run UNDER the preload wrapper so libudev hits the umockdev sandbox, never host /sys.
add_test(NAME devmgr_integration
         COMMAND umockdev-wrapper $<TARGET_FILE:devmgr_integration_tests>)
```

- [ ] **Step 6: Wire the root CMakeLists with the integration gate**

`CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.21)
project(cross_device_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Auto-detect umockdev so the integration suite turns itself on only where it is safe.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(UMOCKDEV IMPORTED_TARGET umockdev-1.0)   # sets UMOCKDEV_FOUND
endif()
option(DEVMGR_BUILD_INTEGRATION_TESTS "Build umockdev integration tests" ${UMOCKDEV_FOUND})

add_subdirectory(core)
add_subdirectory(app)
if(UNIX AND NOT APPLE)
    add_subdirectory(platform/linux)
    add_subdirectory(tui)
endif()
add_subdirectory(tests)
if(DEVMGR_BUILD_INTEGRATION_TESTS)
    add_subdirectory(tests/integration)
endif()
```

- [ ] **Step 7: Link `devmgr_app` into the unit suite**

`tests/CMakeLists.txt` — change the link line (no new test sources yet):
```cmake
target_link_libraries(devmgr_tests PRIVATE devmgr_core devmgr_app GTest::gtest GTest::gtest_main)
```
(Leave the existing `target_include_directories(devmgr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})` — Task 2 adds the platform include path.)

- [ ] **Step 8: Extend the CI globs to the new dirs**

`.github/workflows/ci.yml` — update the two globs:
```yaml
      - name: Check formatting
        run: |
          sudo apt-get update && sudo apt-get install -y clang-format
          find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror
```
and the clang-tidy step command:
```yaml
        run: docker compose -f test/docker-compose.yml run --rm unit bash -c "clang-tidy -p build/linux-debug --warnings-as-errors='*' core/src/*.cpp app/src/*.cpp platform/linux/src/*.cpp"
```

- [ ] **Step 9: Configure (first run rebuilds vcpkg deps incl. ftxui — allow a long timeout)**

Run: `cmake --preset linux-debug`
Expected: configures with no errors; log shows `ftxui` resolved/built by vcpkg and `Found ... libudev`.

- [ ] **Step 10: Build all targets**

Run: `cmake --build --preset linux-debug`
Expected: builds `devmgr_core`, `devmgr_pal_linux`, `devmgr_app`, `devmgr-tui`, `devmgr_tests` with no errors.

- [ ] **Step 11: Run the existing unit suite (unchanged, still green)**

Run: `ctest --test-dir build/linux-debug --output-on-failure`
Expected: all Phase 0 tests PASS (the 19 existing tests). On this host (no umockdev) confirm the configure log printed `DEVMGR_BUILD_INTEGRATION_TESTS` OFF and `devmgr_integration` is **not** in the ctest list.

- [ ] **Step 12: Format**

Run: `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
Then re-build to confirm formatting introduced no breakage: `cmake --build --preset linux-debug`

- [ ] **Step 13: Hand off for commit (do NOT run git yourself)**

Give the user:
```bash
git add vcpkg.json CMakeLists.txt platform app tui tests .github/workflows/ci.yml && \
git commit -m "build(phase1): scaffold devmgr_pal_linux/app/tui targets + gated umockdev integration"
```

---

## Task 2: Real `UdevDeviceEnumerator` (libudev) + umockdev integration test

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp`
- Modify: `platform/linux/src/udev_device_enumerator.cpp`
- Create: `tests/unit/test_udev_field_mapping.cpp` (agent-verifiable, no native deps)
- Modify: `tests/CMakeLists.txt` (add the unit test + the platform include path)
- Modify: `tests/integration/test_udev_enumerator.cpp` (umockdev — user-verified)

**Interfaces:**
- Consumes: `pal::IDeviceEnumerator`, `core::Device`, `core::DeviceId`, `core::BusType`, `core::DeviceStatus`, `core::makeError`.
- Produces: header-only `devmgr::platform_linux` helpers — `std::uint64_t fnv1a64(std::string_view)`, `std::string stableId(std::string_view subsystem, std::string_view syspath, std::string_view vendor, std::string_view product, std::string_view serial)`, `core::BusType busFor(std::string_view subsystem)`, `std::string strip0x(std::string_view)`, `std::string firstNonEmpty(std::initializer_list<const char*>)`; and a working `UdevDeviceEnumerator::enumerate()`.

- [ ] **Step 1: Write the failing unit test for the pure mapping helpers**

`tests/unit/test_udev_field_mapping.cpp`:
```cpp
#include <gtest/gtest.h>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

using namespace devmgr::platform_linux;

TEST(UdevFieldMapping, BusForKnownSubsystems) {
    EXPECT_EQ(busFor("pci"), devmgr::core::BusType::Pci);
    EXPECT_EQ(busFor("usb"), devmgr::core::BusType::Usb);
    EXPECT_EQ(busFor("platform"), devmgr::core::BusType::Platform);
    EXPECT_EQ(busFor("virtio"), devmgr::core::BusType::Virtio);
    EXPECT_EQ(busFor("block"), devmgr::core::BusType::Other);
}

TEST(UdevFieldMapping, Strip0xPrefix) {
    EXPECT_EQ(strip0x("0x8086"), "8086");
    EXPECT_EQ(strip0x("1d6b"), "1d6b");
    EXPECT_EQ(strip0x(""), "");
}

TEST(UdevFieldMapping, StableIdIsDeterministicAndFormatted) {
    const auto a = stableId("usb", "/sys/devices/pci0000:00/usb1", "1d6b", "0002", "");
    const auto b = stableId("usb", "/sys/devices/pci0000:00/usb1", "1d6b", "0002", "");
    EXPECT_EQ(a, b);                              // stable across calls
    EXPECT_EQ(a.rfind("dev-", 0), 0u);            // "dev-" prefix
    EXPECT_EQ(a.size(), 20u);                     // "dev-" + 16 hex chars
}

TEST(UdevFieldMapping, StableIdDistinguishesDevices) {
    EXPECT_NE(stableId("usb", "/sys/a", "1d6b", "0002", ""),
              stableId("usb", "/sys/b", "1d6b", "0002", ""));
}

TEST(UdevFieldMapping, FirstNonEmptyPicksFirstUsableValue) {
    const char* a = nullptr;
    const char* b = "";
    const char* c = "good";
    EXPECT_EQ(firstNonEmpty({a, b, c}), "good");
    EXPECT_EQ(firstNonEmpty({a, b}), "");
}
```

- [ ] **Step 2: Register the unit test and the platform include path**

`tests/CMakeLists.txt` — add the source to `devmgr_tests` and (guarded) the platform include dir so the header-only helpers are visible without linking libudev:
```cmake
add_executable(devmgr_tests
    unit/test_version.cpp
    unit/test_result.cpp
    unit/test_models.cpp
    unit/test_event_bus.cpp
    unit/test_logging.cpp
    unit/test_task_scheduler.cpp
    unit/test_fake_pal.cpp
    unit/test_udev_field_mapping.cpp
)
target_link_libraries(devmgr_tests PRIVATE devmgr_core devmgr_app GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    # Header-only udev mapping helpers (NO libudev link) — unit-test them directly.
    target_include_directories(devmgr_tests PRIVATE ${CMAKE_SOURCE_DIR}/platform/linux/include)
endif()
gtest_discover_tests(devmgr_tests)
```

- [ ] **Step 3: Build to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: FAILS — `fatal error: devmgr/platform/linux/udev_field_mapping.hpp: No such file or directory`.

- [ ] **Step 4: Write the pure mapping helpers (header-only, no `<libudev.h>`)**

`platform/linux/include/devmgr/platform/linux/udev_field_mapping.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include "devmgr/core/models.hpp"

namespace devmgr::platform_linux {

inline std::uint64_t fnv1a64(std::string_view s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

// Deterministic, process-stable device identity (unlike std::hash<string>).
inline std::string stableId(std::string_view subsystem, std::string_view syspath,
                            std::string_view vendor, std::string_view product,
                            std::string_view serial) {
    std::string key;
    key.reserve(subsystem.size() + syspath.size() + vendor.size() + product.size() +
                serial.size() + 4);
    key.append(subsystem).push_back('\x1f');
    key.append(syspath).push_back('\x1f');
    key.append(vendor).push_back(':');
    key.append(product).push_back('\x1f');
    key.append(serial);
    char buf[21];
    std::snprintf(buf, sizeof buf, "dev-%016llx", static_cast<unsigned long long>(fnv1a64(key)));
    return std::string(buf);
}

inline core::BusType busFor(std::string_view subsystem) {
    if (subsystem == "pci") return core::BusType::Pci;
    if (subsystem == "usb") return core::BusType::Usb;
    if (subsystem == "platform") return core::BusType::Platform;
    if (subsystem == "virtio") return core::BusType::Virtio;
    return core::BusType::Other;
}

inline std::string strip0x(std::string_view v) {
    if (v.size() >= 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v.remove_prefix(2);
    return std::string(v);
}

inline std::string firstNonEmpty(std::initializer_list<const char*> candidates) {
    for (const char* c : candidates) {
        if (c != nullptr && c[0] != '\0') return std::string(c);
    }
    return std::string();
}

}  // namespace devmgr::platform_linux
```

- [ ] **Step 5: Build and run the unit test to verify it passes**

Run: `cmake --build --preset linux-debug && ./build/linux-debug/tests/devmgr_tests --gtest_filter='UdevFieldMapping.*'`
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 6: Implement `enumerate()` against libudev (uses the helpers above)**

Replace `platform/linux/src/udev_device_enumerator.cpp` entirely:
```cpp
#include "devmgr/platform/linux/udev_device_enumerator.hpp"

#include <libudev.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "devmgr/platform/linux/udev_field_mapping.hpp"

namespace devmgr::platform_linux {
namespace {

struct UdevCtxDeleter {
    void operator()(udev* p) const noexcept { udev_unref(p); }
};
struct UdevEnumDeleter {
    void operator()(udev_enumerate* p) const noexcept { udev_enumerate_unref(p); }
};
struct UdevDevDeleter {
    void operator()(udev_device* p) const noexcept { udev_device_unref(p); }
};
using UdevCtx = std::unique_ptr<udev, UdevCtxDeleter>;
using UdevEnum = std::unique_ptr<udev_enumerate, UdevEnumDeleter>;
using UdevDev = std::unique_ptr<udev_device, UdevDevDeleter>;
// NOTE: udev_device_get_parent() returns a BORROWED pointer (freed with the
// child). Never wrap it in UdevDev and never unref it.

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
    return stableId(s(udev_device_get_subsystem(d)), s(udev_device_get_syspath(d)),
                    firstNonEmpty({prop(d, "ID_VENDOR_ID"), attr(d, "idVendor"), attr(d, "vendor")}),
                    firstNonEmpty({prop(d, "ID_MODEL_ID"), attr(d, "idProduct"), attr(d, "device")}),
                    firstNonEmpty({prop(d, "ID_SERIAL_SHORT"), prop(d, "ID_SERIAL")}));
}

core::Device mapDevice(udev_device* d) {
    core::Device dev;
    dev.id = core::DeviceId{idFor(d)};
    dev.bus = busFor(s(udev_device_get_subsystem(d)));
    dev.sysfsPath = s(udev_device_get_syspath(d));
    dev.name = firstNonEmpty({prop(d, "ID_MODEL_FROM_DATABASE"), prop(d, "ID_MODEL"),
                              attr(d, "product"), udev_device_get_sysname(d)});
    dev.modalias = firstNonEmpty({prop(d, "MODALIAS"), attr(d, "modalias")});
    dev.vendorId = strip0x(firstNonEmpty({prop(d, "ID_VENDOR_ID"), attr(d, "idVendor"),
                                          attr(d, "vendor")}));
    dev.productId = strip0x(firstNonEmpty({prop(d, "ID_MODEL_ID"), attr(d, "idProduct"),
                                           attr(d, "device")}));
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

}  // namespace

core::Result<std::vector<core::Device>> UdevDeviceEnumerator::enumerate() {
    UdevCtx udev{udev_new()};
    if (!udev) return core::makeError(core::Error::Code::Io, "udev_new() failed");

    UdevEnum en{udev_enumerate_new(udev.get())};
    if (!en) return core::makeError(core::Error::Code::Io, "udev_enumerate_new() failed");

    for (const char* sub : {"pci", "usb", "platform", "virtio"}) {
        udev_enumerate_add_match_subsystem(en.get(), sub);
    }
    if (udev_enumerate_scan_devices(en.get()) < 0) {
        return core::makeError(core::Error::Code::Io, "udev_enumerate_scan_devices failed");
    }

    std::vector<core::Device> out;
    udev_list_entry* entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(en.get())) {
        const char* syspath = udev_list_entry_get_name(entry);  // entry NAME == syspath
        if (syspath == nullptr) continue;

        UdevDev dev{udev_device_new_from_syspath(udev.get(), syspath)};
        if (!dev) {  // FAULT ISOLATION: one bad device never aborts the scan
            core::Device bad;
            bad.id = core::DeviceId{std::string("dev-err-") + syspath};
            bad.sysfsPath = syspath;
            bad.status = core::DeviceStatus::Error;
            bad.errorNote = "udev_device_new_from_syspath failed";
            out.push_back(std::move(bad));
            continue;
        }
        out.push_back(mapDevice(dev.get()));
    }
    return out;
}

}  // namespace devmgr::platform_linux
```

- [ ] **Step 7: Build the enumerator (compile check the agent CAN run)**

Run: `cmake --build --preset linux-debug --target devmgr_pal_linux`
Expected: compiles and links against `PkgConfig::UDEV` with no errors.

- [ ] **Step 8: Write the umockdev integration test (USER-verified in the container)**

Replace `tests/integration/test_udev_enumerator.cpp`:
```cpp
#include <algorithm>

#include <gtest/gtest.h>
#include <umockdev.h>

#include "devmgr/platform/linux/udev_device_enumerator.hpp"

namespace {

class UdevEnumeratorTest : public ::testing::Test {
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

TEST_F(UdevEnumeratorTest, MapsUsbDeviceFieldsAndIsDeterministic) {
    gchar* sys = umockdev_testbed_add_device(
        bed_, "usb", "1-1", nullptr,
        "idVendor", "1d6b", "idProduct", "0002", nullptr,
        "ID_VENDOR_ID", "1d6b", "ID_MODEL_ID", "0002", "SUBSYSTEM", "usb",
        "MODALIAS", "usb:v1D6Bp0002", nullptr);
    ASSERT_NE(sys, nullptr);
    g_free(sys);

    devmgr::platform_linux::UdevDeviceEnumerator enumr;
    auto res = enumr.enumerate();
    ASSERT_TRUE(res.has_value()) << res.error().message;

    const auto& devs = *res;
    auto it = std::find_if(devs.begin(), devs.end(), [](const auto& d) {
        return d.vendorId == "1d6b" && d.productId == "0002";
    });
    ASSERT_NE(it, devs.end());
    EXPECT_EQ(it->bus, devmgr::core::BusType::Usb);
    EXPECT_EQ(it->status, devmgr::core::DeviceStatus::Active);
    EXPECT_NE(it->sysfsPath.find("/devices/"), std::string::npos);
    EXPECT_EQ(it->modalias.rfind("usb:v1D6Bp0002", 0), 0u);

    // Determinism: a second enumeration yields the identical DeviceId.
    auto res2 = enumr.enumerate();
    ASSERT_TRUE(res2.has_value());
    auto it2 = std::find_if(res2->begin(), res2->end(),
                            [&](const auto& d) { return d.id == it->id; });
    EXPECT_NE(it2, res2->end());
}

}  // namespace
```

- [ ] **Step 9: USER runs the container integration suite**

The agent cannot run this (no umockdev on host, no Docker daemon). Hand the user:
```bash
podman-compose -f test/docker-compose.yml run --rm unit   # rootless; CI uses `docker compose`
```
Expected (in container): configure logs `DEVMGR_BUILD_INTEGRATION_TESTS` ON; ctest runs `devmgr_integration` via `umockdev-wrapper`; `UdevEnumeratorTest.MapsUsbDeviceFieldsAndIsDeterministic` PASSES; full suite green.

- [ ] **Step 10: Format + bare-host unit suite green**

Run: `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all unit tests PASS (incl. the 5 new `UdevFieldMapping` tests); `devmgr_tests` still links no native libs.

- [ ] **Step 11: Hand off for commit**

```bash
git add platform/linux tests/unit/test_udev_field_mapping.cpp tests/integration/test_udev_enumerator.cpp tests/CMakeLists.txt && \
git commit -m "feat(pal-linux): real UdevDeviceEnumerator with umockdev integration + unit-tested mapping helpers"
```

---

## Task 3: `DeviceService` — reconcile enumeration deltas + publish events

**Files:**
- Modify: `core/include/devmgr/core/models.hpp` (defaulted `Device` equality)
- Create: `app/include/devmgr/app/device_service.hpp`, `app/src/device_service.cpp`
- Modify: `app/CMakeLists.txt` (replace the placeholder TU)
- Create: `tests/unit/test_device_service.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::Device`, `core::DeviceId`, `core::DeviceAddedEvent/RemovedEvent/ChangedEvent`, `runtime::EventBus`, `test::FakePal`.
- Produces: `class devmgr::app::DeviceService` with ctor `DeviceService(runtime::EventBus&)`, `void applyEnumeration(std::vector<core::Device>)`, `std::vector<core::Device> devices() const`, `std::optional<core::Device> findById(const core::DeviceId&) const`. Also `core::Device` gains `operator==`.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_device_service.cpp`:
```cpp
#include <atomic>

#include <gtest/gtest.h>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/event_bus.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id, std::string name = "n") {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}
}  // namespace

TEST(DeviceServiceTest, FirstEnumerationEmitsAddedPerDevice) {
    runtime::EventBus bus;
    std::atomic<int> added{0}, removed{0}, changed{0};
    auto s1 = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto s2 = bus.subscribe<core::DeviceRemovedEvent>([&](const auto&) { ++removed; });
    auto s3 = bus.subscribe<core::DeviceChangedEvent>([&](const auto&) { ++changed; });

    app::DeviceService svc(bus);
    svc.applyEnumeration({dev("a"), dev("b")});

    EXPECT_EQ(added.load(), 2);
    EXPECT_EQ(removed.load(), 0);
    EXPECT_EQ(changed.load(), 0);
    EXPECT_EQ(svc.devices().size(), 2u);
}

TEST(DeviceServiceTest, ReapplyingIdenticalSnapshotEmitsNothing) {
    runtime::EventBus bus;
    std::atomic<int> events{0};
    auto s1 = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++events; });
    auto s2 = bus.subscribe<core::DeviceRemovedEvent>([&](const auto&) { ++events; });
    auto s3 = bus.subscribe<core::DeviceChangedEvent>([&](const auto&) { ++events; });

    app::DeviceService svc(bus);
    svc.applyEnumeration({dev("a"), dev("b")});
    events = 0;
    svc.applyEnumeration({dev("a"), dev("b")});  // identical

    EXPECT_EQ(events.load(), 0);
}

TEST(DeviceServiceTest, DeltaEmitsAddedRemovedChanged) {
    runtime::EventBus bus;
    std::atomic<int> added{0}, removed{0}, changed{0};
    auto s1 = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });
    auto s2 = bus.subscribe<core::DeviceRemovedEvent>([&](const auto&) { ++removed; });
    auto s3 = bus.subscribe<core::DeviceChangedEvent>([&](const auto&) { ++changed; });

    app::DeviceService svc(bus);
    svc.applyEnumeration({dev("a", "old"), dev("b")});
    added = removed = changed = 0;
    // 'a' name changes, 'b' removed, 'c' added.
    svc.applyEnumeration({dev("a", "new"), dev("c")});

    EXPECT_EQ(added.load(), 1);
    EXPECT_EQ(removed.load(), 1);
    EXPECT_EQ(changed.load(), 1);
    EXPECT_EQ(svc.findById(core::DeviceId{"a"})->name, "new");
    EXPECT_FALSE(svc.findById(core::DeviceId{"b"}).has_value());
}

TEST(DeviceServiceTest, NoDeadlockWhenHandlerReadsDevicesDuringPublish) {
    runtime::EventBus bus;
    app::DeviceService svc(bus);
    std::atomic<std::size_t> seen{0};
    auto sub = bus.subscribe<core::DeviceAddedEvent>(
        [&](const auto&) { seen = svc.devices().size(); });  // read during publish
    svc.applyEnumeration({dev("a")});
    EXPECT_EQ(seen.load(), 1u);
}
```

- [ ] **Step 2: Register the test and replace the app placeholder source**

`tests/CMakeLists.txt` — add `unit/test_device_service.cpp` to the `devmgr_tests` source list.
`app/CMakeLists.txt` — replace `src/placeholder.cpp` with `src/device_service.cpp`:
```cmake
add_library(devmgr_app STATIC src/device_service.cpp)
```

- [ ] **Step 3: Build to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: FAILS — `fatal error: devmgr/app/device_service.hpp: No such file or directory`.

- [ ] **Step 4: Add defaulted equality to `Device`**

`core/include/devmgr/core/models.hpp` — inside `struct Device`, after the data members and before the closing brace:
```cpp
    friend bool operator==(const Device&, const Device&) = default;
```
(Memberwise comparison: covers all fields including `properties` and the `optional<DeviceId>`/`DeviceId` members, reusing `DeviceId`'s existing `operator==`.)

- [ ] **Step 5: Write the `DeviceService` header**

`app/include/devmgr/app/device_service.hpp`:
```cpp
#pragma once
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Owns the in-memory device model and turns enumeration snapshots into
// EventBus deltas. Thread-safe; events are published WITHOUT holding the mutex.
class DeviceService {
   public:
    explicit DeviceService(runtime::EventBus& bus) : bus_(bus) {}

    void applyEnumeration(std::vector<core::Device> snapshot);
    std::vector<core::Device> devices() const;
    std::optional<core::Device> findById(const core::DeviceId& id) const;

   private:
    runtime::EventBus& bus_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, core::Device> model_;  // keyed by DeviceId.value
};

}  // namespace devmgr::app
```

- [ ] **Step 6: Write the `DeviceService` implementation**

`app/src/device_service.cpp`:
```cpp
#include "devmgr/app/device_service.hpp"

#include <unordered_map>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {

void DeviceService::applyEnumeration(std::vector<core::Device> snapshot) {
    std::vector<core::Device> added;
    std::vector<core::Device> changed;
    std::vector<core::DeviceId> removed;

    {
        std::scoped_lock lock(mutex_);
        std::unordered_map<std::string, core::Device> next;
        next.reserve(snapshot.size());
        for (auto& d : snapshot) {
            const std::string key = d.id.value;
            auto prev = model_.find(key);
            if (prev == model_.end()) {
                added.push_back(d);
            } else if (!(prev->second == d)) {
                changed.push_back(d);
            }
            next.emplace(key, std::move(d));
        }
        for (const auto& [key, dev] : model_) {
            if (next.find(key) == next.end()) removed.push_back(dev.id);
        }
        model_.swap(next);
    }

    // Publish outside the lock: EventBus invokes handlers synchronously and a
    // handler may call back into devices()/findById().
    for (const auto& id : removed) bus_.publish(core::DeviceRemovedEvent{id});
    for (auto& d : added) bus_.publish(core::DeviceAddedEvent{std::move(d)});
    for (auto& d : changed) bus_.publish(core::DeviceChangedEvent{std::move(d)});
}

std::vector<core::Device> DeviceService::devices() const {
    std::scoped_lock lock(mutex_);
    std::vector<core::Device> out;
    out.reserve(model_.size());
    for (const auto& [key, dev] : model_) out.push_back(dev);
    return out;
}

std::optional<core::Device> DeviceService::findById(const core::DeviceId& id) const {
    std::scoped_lock lock(mutex_);
    auto it = model_.find(id.value);
    if (it == model_.end()) return std::nullopt;
    return it->second;
}

}  // namespace devmgr::app
```

- [ ] **Step 7: Build and run to verify it passes**

Run: `cmake --build --preset linux-debug && ./build/linux-debug/tests/devmgr_tests --gtest_filter='DeviceServiceTest.*'`
Expected: `[  PASSED  ] 4 tests.`

- [ ] **Step 8: Format + full suite + hand off for commit**

Run: `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`  (all green)
```bash
git add core/include/devmgr/core/models.hpp app tests/unit/test_device_service.cpp tests/CMakeLists.txt && \
git commit -m "feat(app): DeviceService reconciles enumeration deltas into EventBus events"
```

---

## Task 4: `IUiDispatcher` + `ApplicationFacade` read API

**Files:**
- Create: `app/include/devmgr/app/ui_dispatcher.hpp`
- Create: `app/include/devmgr/app/application_facade.hpp`, `app/src/application_facade.cpp`
- Modify: `app/CMakeLists.txt`
- Create: `tests/fakes/inline_ui_dispatcher.hpp`
- Create: `tests/unit/test_application_facade.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pal::IDeviceEnumerator`, `runtime::TaskScheduler`, `runtime::EventBus`, `app::DeviceService`, `core::ErrorEvent`, `test::FakePal`.
- Produces: `class devmgr::app::IUiDispatcher { virtual void post(std::function<void()>) = 0; }`; `class devmgr::app::ApplicationFacade` with ctor `ApplicationFacade(pal::IDeviceEnumerator&, runtime::TaskScheduler&, runtime::EventBus&, DeviceService&)`, `std::future<void> refresh()`, `std::vector<core::Device> devices() const`, `std::optional<core::Device> findById(const core::DeviceId&) const`; test double `devmgr::test::InlineUiDispatcher`.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_application_facade.cpp`:
```cpp
#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.status = core::DeviceStatus::Active;
    return d;
}

// Enumerator that always fails — exercises the ErrorEvent path.
class FailingEnumerator final : public pal::IDeviceEnumerator {
   public:
    core::Result<std::vector<core::Device>> enumerate() override {
        return core::makeError(core::Error::Code::Io, "boom");
    }
};
}  // namespace

TEST(ApplicationFacadeTest, RefreshPopulatesModelAndEmitsAdded) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    pal.seedDevice(dev("a"));
    pal.seedDevice(dev("b"));
    app::DeviceService svc(bus);
    std::atomic<int> added{0};
    auto sub = bus.subscribe<core::DeviceAddedEvent>([&](const auto&) { ++added; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    EXPECT_EQ(added.load(), 2);
    EXPECT_EQ(facade.devices().size(), 2u);
    EXPECT_TRUE(facade.findById(core::DeviceId{"a"}).has_value());
}

TEST(ApplicationFacadeTest, RefreshErrorEmitsErrorEventAndLeavesModelIntact) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    FailingEnumerator pal;
    app::DeviceService svc(bus);
    std::atomic<int> errors{0};
    auto sub = bus.subscribe<core::ErrorEvent>([&](const auto&) { ++errors; });

    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    EXPECT_EQ(errors.load(), 1);
    EXPECT_EQ(facade.devices().size(), 0u);
}
```

- [ ] **Step 2: Register the test + add the facade source**

`tests/CMakeLists.txt` — add `unit/test_application_facade.cpp`.
`app/CMakeLists.txt` — add the source:
```cmake
add_library(devmgr_app STATIC src/device_service.cpp src/application_facade.cpp)
```

- [ ] **Step 3: Build to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: FAILS — `fatal error: devmgr/app/application_facade.hpp: No such file or directory`.

- [ ] **Step 4: Write `IUiDispatcher`**

`app/include/devmgr/app/ui_dispatcher.hpp`:
```cpp
#pragma once
#include <functional>

namespace devmgr::app {

// Marshals a callback onto the frontend's UI thread. The concrete impl lives in
// each frontend (FTXUI / Qt); the app layer depends only on this interface.
class IUiDispatcher {
   public:
    virtual ~IUiDispatcher() = default;
    virtual void post(std::function<void()> fn) = 0;
};

}  // namespace devmgr::app
```

- [ ] **Step 5: Write the `ApplicationFacade` header + impl**

`app/include/devmgr/app/application_facade.hpp`:
```cpp
#pragma once
#include <future>
#include <optional>
#include <vector>

#include "devmgr/app/device_service.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"

namespace devmgr::app {

// The single command/read surface the frontends use. refresh() runs enumeration
// on the TaskScheduler so the UI thread never blocks on I/O.
class ApplicationFacade {
   public:
    ApplicationFacade(pal::IDeviceEnumerator& enumerator, runtime::TaskScheduler& scheduler,
                      runtime::EventBus& bus, DeviceService& service)
        : enumerator_(enumerator), scheduler_(scheduler), bus_(bus), service_(service) {}

    std::future<void> refresh();
    std::vector<core::Device> devices() const { return service_.devices(); }
    std::optional<core::Device> findById(const core::DeviceId& id) const {
        return service_.findById(id);
    }

   private:
    pal::IDeviceEnumerator& enumerator_;
    runtime::TaskScheduler& scheduler_;
    runtime::EventBus& bus_;
    DeviceService& service_;
};

}  // namespace devmgr::app
```

`app/src/application_facade.cpp`:
```cpp
#include "devmgr/app/application_facade.hpp"

#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {

std::future<void> ApplicationFacade::refresh() {
    return scheduler_.submit([this] {
        auto result = enumerator_.enumerate();
        if (result) {
            service_.applyEnumeration(std::move(*result));
        } else {
            bus_.publish(core::ErrorEvent{"enumerate", result.error().message});
        }
    });
}

}  // namespace devmgr::app
```

- [ ] **Step 6: Write the inline dispatcher test double (used here + Task 5)**

`tests/fakes/inline_ui_dispatcher.hpp`:
```cpp
#pragma once
#include <functional>
#include <utility>

#include "devmgr/app/ui_dispatcher.hpp"

namespace devmgr::test {

// Runs the posted closure immediately on the calling thread — deterministic
// for unit tests (no real UI thread to marshal onto).
class InlineUiDispatcher final : public app::IUiDispatcher {
   public:
    void post(std::function<void()> fn) override { fn(); }
};

}  // namespace devmgr::test
```

- [ ] **Step 7: Build and run to verify it passes**

Run: `cmake --build --preset linux-debug && ./build/linux-debug/tests/devmgr_tests --gtest_filter='ApplicationFacadeTest.*'`
Expected: `[  PASSED  ] 2 tests.`

- [ ] **Step 8: Format + full suite + hand off for commit**

Run: `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`  (all green)
```bash
git add app tests/fakes/inline_ui_dispatcher.hpp tests/unit/test_application_facade.cpp tests/CMakeLists.txt && \
git commit -m "feat(app): IUiDispatcher + ApplicationFacade TaskScheduler-driven refresh"
```

---

## Task 5: `DeviceListVM` + `DeviceDetailVM`

**Files:**
- Create: `app/include/devmgr/app/device_list_vm.hpp`, `app/src/device_list_vm.cpp`
- Create: `app/include/devmgr/app/device_detail_vm.hpp`, `app/src/device_detail_vm.cpp`
- Modify: `app/CMakeLists.txt`
- Create: `tests/unit/test_device_list_vm.cpp`, `tests/unit/test_device_detail_vm.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `app::ApplicationFacade`, `app::IUiDispatcher`, `runtime::EventBus`, `core::Device*Event`, `test::FakePal`, `test::InlineUiDispatcher`.
- Produces:
  - `class devmgr::app::DeviceListVM` — ctor `(ApplicationFacade&, runtime::EventBus&, IUiDispatcher&)`; `const std::vector<std::string>& rowsRef() const`; `std::vector<std::string>& rowsRef()`; `int& selectedRef()`; `void setFilter(std::string)`; `std::optional<core::DeviceId> selectedDeviceId() const`; `void rebuild()`.
  - `class devmgr::app::DeviceDetailVM` — ctor `(ApplicationFacade&)`; `std::vector<std::string> lines(const std::optional<core::DeviceId>&) const`.

- [ ] **Step 1: Write the failing tests**

`tests/unit/test_device_list_vm.cpp`:
```cpp
#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id, core::BusType bus, std::string name) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.bus = bus;
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}

struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    test::InlineUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc};
};
}  // namespace

TEST(DeviceListVmTest, RowsGroupedByBusAfterRefresh) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "GPU"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);

    f.facade.refresh().wait();  // InlineUiDispatcher applies the rebuild synchronously

    // PCI group precedes USB group; group headers present.
    const auto& rows = vm.rowsRef();
    ASSERT_GE(rows.size(), 4u);  // 2 headers + 2 devices
    auto idxPci = -1, idxUsb = -1, idxGpu = -1, idxMouse = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (rows[i].find("PCI") != std::string::npos) idxPci = i;
        if (rows[i].find("USB") != std::string::npos) idxUsb = i;
        if (rows[i].find("GPU") != std::string::npos) idxGpu = i;
        if (rows[i].find("Mouse") != std::string::npos) idxMouse = i;
    }
    EXPECT_LT(idxPci, idxUsb);
    EXPECT_LT(idxGpu, idxMouse);
}

TEST(DeviceListVmTest, FilterNarrowsCaseInsensitivelyAndClampsSelection) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Logitech Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "NVIDIA GPU"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    vm.selectedRef() = static_cast<int>(vm.rowsRef().size()) - 1;
    vm.setFilter("mouse");  // case-insensitive

    bool anyGpu = false;
    for (const auto& r : vm.rowsRef()) {
        if (r.find("GPU") != std::string::npos) anyGpu = true;
    }
    EXPECT_FALSE(anyGpu);
    EXPECT_LT(vm.selectedRef(), static_cast<int>(vm.rowsRef().size()));  // clamped
    EXPECT_GE(vm.selectedRef(), 0);
}

TEST(DeviceListVmTest, SelectedDeviceIdMapsRowsAndIsNulloptOnHeader) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    // Header row (index 0) → nullopt; the device row → its id.
    vm.selectedRef() = 0;
    EXPECT_FALSE(vm.selectedDeviceId().has_value());
    for (int i = 0; i < static_cast<int>(vm.rowsRef().size()); ++i) {
        vm.selectedRef() = i;
        if (vm.rowsRef()[i].find("Mouse") != std::string::npos) {
            ASSERT_TRUE(vm.selectedDeviceId().has_value());
            EXPECT_EQ(vm.selectedDeviceId()->value, "u1");
        }
    }
}
```

`tests/unit/test_device_detail_vm.cpp`:
```cpp
#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"

using namespace devmgr;

TEST(DeviceDetailVmTest, RendersLabeledLinesForSelectedDevice) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    core::Device d;
    d.id = core::DeviceId{"u1"};
    d.bus = core::BusType::Usb;
    d.name = "Mouse";
    d.vendorId = "1d6b";
    d.productId = "0002";
    d.status = core::DeviceStatus::Active;
    pal.seedDevice(d);
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    facade.refresh().wait();

    app::DeviceDetailVM vm(facade);
    auto lines = vm.lines(core::DeviceId{"u1"});

    bool hasName = false, hasVidPid = false;
    for (const auto& l : lines) {
        if (l.find("Mouse") != std::string::npos) hasName = true;
        if (l.find("1d6b") != std::string::npos && l.find("0002") != std::string::npos)
            hasVidPid = true;
    }
    EXPECT_TRUE(hasName);
    EXPECT_TRUE(hasVidPid);
}

TEST(DeviceDetailVmTest, EmptySelectionYieldsPlaceholder) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler(2);
    test::FakePal pal;
    app::DeviceService svc(bus);
    app::ApplicationFacade facade(pal, scheduler, bus, svc);
    app::DeviceDetailVM vm(facade);

    auto lines = vm.lines(std::nullopt);
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines.front().find("no device"), std::string::npos);
}
```

- [ ] **Step 2: Register tests + add VM sources**

`tests/CMakeLists.txt` — add `unit/test_device_list_vm.cpp` and `unit/test_device_detail_vm.cpp`.
`app/CMakeLists.txt`:
```cmake
add_library(devmgr_app STATIC
    src/device_service.cpp
    src/application_facade.cpp
    src/device_list_vm.cpp
    src/device_detail_vm.cpp)
```

- [ ] **Step 3: Build to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: FAILS — `fatal error: devmgr/app/device_list_vm.hpp: No such file or directory`.

- [ ] **Step 4: Write `DeviceDetailVM` (header + impl)**

`app/include/devmgr/app/device_detail_vm.hpp`:
```cpp
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/core/models.hpp"

namespace devmgr::app {

// Stateless view over the facade: turns a selected DeviceId into labeled lines.
class DeviceDetailVM {
   public:
    explicit DeviceDetailVM(ApplicationFacade& facade) : facade_(facade) {}
    std::vector<std::string> lines(const std::optional<core::DeviceId>& id) const;

   private:
    ApplicationFacade& facade_;
};

}  // namespace devmgr::app
```

`app/src/device_detail_vm.cpp`:
```cpp
#include "devmgr/app/device_detail_vm.hpp"

namespace devmgr::app {

std::vector<std::string> DeviceDetailVM::lines(const std::optional<core::DeviceId>& id) const {
    if (!id.has_value()) return {"(no device selected)"};
    auto dev = facade_.findById(*id);
    if (!dev.has_value()) return {"(no device selected)"};

    const core::Device& d = *dev;
    std::vector<std::string> out;
    out.push_back(std::string("Name:    ") + d.name);
    out.push_back(std::string("Id:      ") + d.id.value);
    out.push_back(std::string("Bus:     ") + core::to_string(d.bus));
    out.push_back(std::string("Status:  ") + core::to_string(d.status));
    out.push_back(std::string("Sysfs:   ") + d.sysfsPath);
    out.push_back(std::string("VID:PID: ") + d.vendorId + ":" + d.productId);
    out.push_back(std::string("Serial:  ") + d.serial);
    out.push_back(std::string("Driver:  ") + d.boundDriver.value_or("(none)"));
    out.push_back(std::string("Modalias:") + d.modalias);
    if (d.parent.has_value()) out.push_back(std::string("Parent:  ") + d.parent->value);
    if (d.errorNote.has_value()) out.push_back(std::string("Error:   ") + *d.errorNote);
    return out;
}

}  // namespace devmgr::app
```

- [ ] **Step 5: Write `DeviceListVM` (header + impl)**

`app/include/devmgr/app/device_list_vm.hpp`:
```cpp
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/ui_dispatcher.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/runtime/event_bus.hpp"

namespace devmgr::app {

// Builds the FTXUI-facing row list (grouped/sorted by bus, filtered) and maps
// the selected row to a DeviceId. Subscribes to model deltas and rebuilds via
// the dispatcher, so all state mutation happens on the UI thread.
class DeviceListVM {
   public:
    DeviceListVM(ApplicationFacade& facade, runtime::EventBus& bus, IUiDispatcher& dispatcher);

    std::vector<std::string>& rowsRef() { return rows_; }
    const std::vector<std::string>& rowsRef() const { return rows_; }
    int& selectedRef() { return selected_; }
    void setFilter(std::string filter);
    std::optional<core::DeviceId> selectedDeviceId() const;
    void rebuild();  // UI-thread: re-read model, filter, group, clamp selection

   private:
    void onModelChanged();  // EventBus handler — marshals rebuild() via dispatcher

    ApplicationFacade& facade_;
    IUiDispatcher& dispatcher_;
    std::string filter_;
    std::vector<std::string> rows_;
    std::vector<std::optional<core::DeviceId>> rowIds_;  // nullopt == group header
    int selected_ = 0;
    runtime::Subscription subAdded_;
    runtime::Subscription subRemoved_;
    runtime::Subscription subChanged_;
};

}  // namespace devmgr::app
```

`app/src/device_list_vm.cpp`:
```cpp
#include "devmgr/app/device_list_vm.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

#include "devmgr/core/events.hpp"

namespace devmgr::app {
namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matchesFilter(const core::Device& d, const std::string& needleLower) {
    if (needleLower.empty()) return true;
    std::string hay = toLower(d.name + " " + d.vendorId + ":" + d.productId + " " +
                              core::to_string(d.bus));
    return hay.find(needleLower) != std::string::npos;
}

}  // namespace

DeviceListVM::DeviceListVM(ApplicationFacade& facade, runtime::EventBus& bus,
                           IUiDispatcher& dispatcher)
    : facade_(facade), dispatcher_(dispatcher) {
    subAdded_ = bus.subscribe<core::DeviceAddedEvent>([this](const auto&) { onModelChanged(); });
    subRemoved_ =
        bus.subscribe<core::DeviceRemovedEvent>([this](const auto&) { onModelChanged(); });
    subChanged_ =
        bus.subscribe<core::DeviceChangedEvent>([this](const auto&) { onModelChanged(); });
}

void DeviceListVM::onModelChanged() {
    // Handler may run on a TaskScheduler worker — marshal the rebuild to the UI thread.
    dispatcher_.post([this] { rebuild(); });
}

void DeviceListVM::setFilter(std::string filter) {
    filter_ = std::move(filter);
    rebuild();  // called on the UI thread (Input.on_change)
}

void DeviceListVM::rebuild() {
    static constexpr std::array<core::BusType, 5> kOrder = {
        core::BusType::Pci, core::BusType::Usb, core::BusType::Platform, core::BusType::Virtio,
        core::BusType::Other};

    const std::string needle = toLower(filter_);
    auto devices = facade_.devices();

    rows_.clear();
    rowIds_.clear();
    for (core::BusType bus : kOrder) {
        std::vector<core::Device> group;
        for (auto& d : devices) {
            if (d.bus == bus && matchesFilter(d, needle)) group.push_back(d);
        }
        if (group.empty()) continue;
        std::sort(group.begin(), group.end(),
                  [](const core::Device& a, const core::Device& b) { return a.name < b.name; });
        rows_.push_back(std::string("── ") + core::to_string(bus) + " ──");
        rowIds_.emplace_back(std::nullopt);  // header
        for (const auto& d : group) {
            rows_.push_back("  " + d.name + "  (" + d.vendorId + ":" + d.productId + ")");
            rowIds_.emplace_back(d.id);
        }
    }

    if (rows_.empty()) {
        rows_.push_back("(no devices)");
        rowIds_.emplace_back(std::nullopt);
    }
    if (selected_ < 0) selected_ = 0;
    if (selected_ >= static_cast<int>(rows_.size())) selected_ = static_cast<int>(rows_.size()) - 1;
}

std::optional<core::DeviceId> DeviceListVM::selectedDeviceId() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(rowIds_.size())) return std::nullopt;
    return rowIds_[selected_];
}

}  // namespace devmgr::app
```

- [ ] **Step 6: Build and run to verify they pass**

Run: `cmake --build --preset linux-debug && ./build/linux-debug/tests/devmgr_tests --gtest_filter='DeviceListVmTest.*:DeviceDetailVmTest.*'`
Expected: `[  PASSED  ] 5 tests.`

- [ ] **Step 7: Format + full suite + hand off for commit**

Run: `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`  (all green)
```bash
git add app tests/unit/test_device_list_vm.cpp tests/unit/test_device_detail_vm.cpp tests/CMakeLists.txt && \
git commit -m "feat(app): DeviceListVM (group/filter/select) + DeviceDetailVM"
```

---

## Task 6: FTXUI `devmgr-tui` frontend + `FtxuiUiDispatcher`

**Files:**
- Create: `tui/src/ftxui_ui_dispatcher.hpp`, `tui/src/ftxui_ui_dispatcher.cpp`
- Create: `tui/src/tui_app.hpp`, `tui/src/tui_app.cpp`
- Modify: `tui/src/main.cpp`, `tui/CMakeLists.txt`

**Interfaces:**
- Consumes: `app::IUiDispatcher`, `app::ApplicationFacade`, `app::DeviceService`, `app::DeviceListVM`, `app::DeviceDetailVM`, `platform_linux::UdevDeviceEnumerator`, `runtime::EventBus`, `runtime::TaskScheduler`, FTXUI 6.1.9.
- Produces: `class FtxuiUiDispatcher : app::IUiDispatcher` with ctor `(ftxui::ScreenInteractive&)`, `post(...)`, `drain()`; `int runTuiApp()` composition root.

- [ ] **Step 1: Write the `FtxuiUiDispatcher`**

`tui/src/ftxui_ui_dispatcher.hpp`:
```cpp
#pragma once
#include <functional>
#include <mutex>
#include <queue>

#include <ftxui/component/screen_interactive.hpp>

#include "devmgr/app/ui_dispatcher.hpp"

namespace devmgr::tui {

// IUiDispatcher over FTXUI: post() enqueues + wakes the loop with Event::Custom;
// drain() runs the queued closures on the UI thread. (FTXUI 6.1.9 also offers
// ScreenInteractive::Post(Closure); the queue+PostEvent variant is the locked design.)
class FtxuiUiDispatcher final : public app::IUiDispatcher {
   public:
    explicit FtxuiUiDispatcher(ftxui::ScreenInteractive& screen) : screen_(screen) {}

    void post(std::function<void()> fn) override {
        {
            std::scoped_lock lock(mutex_);
            queue_.push(std::move(fn));
        }
        screen_.PostEvent(ftxui::Event::Custom);
    }

    void drain() {
        std::queue<std::function<void()>> local;
        {
            std::scoped_lock lock(mutex_);
            std::swap(local, queue_);
        }
        while (!local.empty()) {
            local.front()();
            local.pop();
        }
    }

   private:
    ftxui::ScreenInteractive& screen_;
    std::mutex mutex_;
    std::queue<std::function<void()>> queue_;
};

}  // namespace devmgr::tui
```

`tui/src/ftxui_ui_dispatcher.cpp`:
```cpp
#include "tui/src/ftxui_ui_dispatcher.hpp"
// Header-only behavior; this TU exists so the class has a home for future
// non-inline members and to keep the build target explicit.
namespace devmgr::tui { }
```

- [ ] **Step 2: Write the composition root + component tree**

`tui/src/tui_app.hpp`:
```cpp
#pragma once
namespace devmgr::tui {
int runTuiApp();
}  // namespace devmgr::tui
```

`tui/src/tui_app.cpp`:
```cpp
#include "tui/src/tui_app.hpp"

#include <string>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "tui/src/ftxui_ui_dispatcher.hpp"

namespace devmgr::tui {

int runTuiApp() {
    using namespace ftxui;

    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    platform_linux::UdevDeviceEnumerator enumerator;
    app::DeviceService service(bus);
    app::ApplicationFacade facade(enumerator, scheduler, bus, service);

    auto screen = ScreenInteractive::Fullscreen();
    FtxuiUiDispatcher dispatcher(screen);
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);

    std::string filter;
    InputOption inputOpt;
    inputOpt.content = &filter;
    inputOpt.placeholder = "filter devices…";
    inputOpt.on_change = [&] { listVm.setFilter(filter); };
    auto searchInput = Input(inputOpt);

    auto deviceMenu = Menu(&listVm.rowsRef(), &listVm.selectedRef(), MenuOption::Vertical());

    auto leftPane = Container::Vertical({searchInput, deviceMenu});

    auto detailRenderer = Renderer([&] {
        Elements els;
        for (const auto& line : detailVm.lines(listVm.selectedDeviceId())) {
            els.push_back(text(line));
        }
        return vbox(std::move(els)) | flex;
    });

    auto layout = Container::Horizontal({leftPane, detailRenderer});
    auto ui = Renderer(layout, [&] {
        return hbox({
                   vbox({
                       text(" Devices (r=refresh  q=quit) ") | bold,
                       separator(),
                       searchInput->Render(),
                       separator(),
                       deviceMenu->Render() | vscroll_indicator | frame | flex,
                   }) | size(WIDTH, EQUAL, 44) |
                       border,
                   detailRenderer->Render() | border | flex,
               }) |
               flex;
    });

    auto root = CatchEvent(ui, [&](Event event) {
        if (event == Event::Custom) {  // worker posted a UI update
            dispatcher.drain();
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
        if (event == Event::Character('r')) {
            facade.refresh();  // fire-and-forget; results arrive via the dispatcher
            return true;
        }
        return false;  // let Input / Menu handle the rest (incl. mouse)
    });

    facade.refresh();  // initial populate without pressing 'r'
    screen.Loop(root);
    return 0;
}

}  // namespace devmgr::tui
```

- [ ] **Step 3: Replace `main.cpp` and update the TUI CMakeLists**

`tui/src/main.cpp`:
```cpp
#include "tui/src/tui_app.hpp"

int main() {
    return devmgr::tui::runTuiApp();
}
```

`tui/CMakeLists.txt`:
```cmake
find_package(ftxui CONFIG REQUIRED)

add_executable(devmgr-tui
    src/main.cpp
    src/tui_app.cpp
    src/ftxui_ui_dispatcher.cpp)
target_include_directories(devmgr-tui PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr-tui
    PRIVATE devmgr_app devmgr_core devmgr_pal_linux
            ftxui::component ftxui::dom ftxui::screen)
target_compile_features(devmgr-tui PRIVATE cxx_std_20)
```

- [ ] **Step 4: Build the TUI (compile/link check the agent CAN run)**

Run: `cmake --build --preset linux-debug --target devmgr-tui`
Expected: builds and links with no errors.

- [ ] **Step 5: Confirm the unit suite is still green and format**

Run: `find core tests app platform tui -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i`
Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all unit tests PASS.

- [ ] **Step 6: USER runs the manual TUI smoke (real Linux host)**

The agent cannot drive an interactive TTY or read real hardware. Hand the user:
```bash
./build/linux-debug/tui/devmgr-tui
```
Confirm: the list auto-populates at startup, grouped/sorted by bus; arrow keys + mouse wheel scroll; click/Enter selects a row and updates the detail pane; typing in the filter narrows the list case-insensitively and selection stays valid; `r` re-enumerates (plug/unplug a USB device, press `r`, list updates); `q`/Esc exits cleanly leaving the terminal sane. Cross-check device names/counts against `lspci`, `lsusb`, `udevadm info --export-db`.

- [ ] **Step 7: Hand off for commit**

```bash
git add tui && git commit -m "feat(tui): FTXUI devmgr-tui — list/detail/refresh/search/group-by-bus/mouse"
```

---

## Self-Review

**1. Spec coverage** (against `2026-06-29-phase1-read-only-enumeration-tui-design.md`):
- Target graph (`devmgr_pal_linux`/`devmgr_app`/`devmgr-tui`/gated integration) → Task 1. ✓
- `UdevDeviceEnumerator` + field mapping + fault isolation + FNV-1a id + borrowed-parent → Task 2 (+ helper unit tests + umockdev integration). ✓
- `DeviceService` reconcile + `Device operator==` → Task 3. ✓
- `IUiDispatcher` + `ApplicationFacade` refresh + ErrorEvent + inline double → Task 4. ✓
- `DeviceListVM` (group/sort/filter/selection clamp) + `DeviceDetailVM` → Task 5. ✓
- FTXUI TUI + `FtxuiUiDispatcher` (PostEvent+drain) + refresh/search/group/mouse + initial refresh → Task 6. ✓
- Testing: bare-host unit suite (no native deps), gated umockdev container run, manual smoke + ground-truth cross-check → covered across tasks + steps. ✓
- Constraints: libudev PRIVATE, namespace `devmgr::platform_linux`, no-commit policy, clang-format, CI glob extension → Global Constraints + Task 1 Step 8. ✓

**2. Placeholder scan:** No "TBD"/"handle edge cases"/"similar to Task N" — every code step shows complete code; the only intentional stubs (Task 1) are explicit, compilable, and replaced in later tasks.

**3. Type consistency:** `applyEnumeration`, `devices()`, `findById`, `refresh()→std::future<void>`, `IUiDispatcher::post`, `DeviceListVM::rowsRef/selectedRef/setFilter/selectedDeviceId/rebuild`, `DeviceDetailVM::lines`, `FtxuiUiDispatcher::post/drain` are used identically in their tests, headers, and the TUI composition root. `Device operator==` (Task 3) is the equality `DeviceService` relies on. Namespaces consistent (`devmgr::platform_linux`, `devmgr::app`, `devmgr::tui`, `devmgr::test`).

---

## Execution Handoff

Plan complete. Two execution options:

1. **Subagent-Driven (recommended)** — a fresh subagent per task with two-stage review between tasks (matches the project's established SDD cadence; the user commits each task before the next).
2. **Inline Execution** — execute tasks in this session with checkpoints for review.
