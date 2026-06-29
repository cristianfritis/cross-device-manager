# Phase 0 — Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the build system, the OS-agnostic core library (domain types, event bus, async task scheduler, logging), the Platform Abstraction Layer interfaces with an in-memory fake, and the Docker/VM/CI test harness — a green, independently-testable foundation every later phase builds on.

**Architecture:** A single static library `devmgr_core` exposes pure-virtual PAL interfaces, domain models, a thread-safe `EventBus`, and a `TaskScheduler` thread pool. No OS or UI code lands in Phase 0 — the only PAL implementation is an in-memory `FakePal` used by unit tests. Everything is exercised by a GoogleTest suite run locally, in Docker, and (scaffolded) in a disposable VM.

**Tech Stack:** C++20, CMake ≥ 3.21, vcpkg (manifest mode), GoogleTest, spdlog, nlohmann-json, tl-expected, clang-format, clang-tidy.

## Global Constraints

Copied verbatim from the approved architecture plan; every task implicitly inherits these:

- **Language:** C++17/20. This plan targets **C++20** (`CMAKE_CXX_STANDARD 20`, extensions OFF).
- **Single Responsibility Principle:** one clear responsibility per file/class.
- **Functions small, well-named, do one thing. No deep nesting — use early returns / guard clauses.**
- **Platform Abstraction Layer:** core logic is OS-agnostic; it depends only on PAL interfaces, never on OS APIs.
- **UI Abstraction Layer:** core logic never knows whether it talks to a TUI or GUI. (No UI code in Phase 0.)
- **Modern C++:** `std::filesystem`, smart pointers, RAII — no raw owning pointers, no manual resource cleanup.
- **Event-driven:** async callbacks/events for long-running work and hotplug.
- **Uniform fallible return:** `Result<T> = tl::expected<T, Error>`. No exceptions for expected failures.
- **Process discipline:** DRY, YAGNI, TDD (test first), frequent commits.

---

## File Structure

```
CMakeLists.txt                              # root: project, C++20, subdirs, ctest
CMakePresets.json                           # linux-debug/-release presets via vcpkg toolchain
vcpkg.json                                  # manifest: gtest, spdlog, nlohmann-json, tl-expected
.clang-format                               # style
.clang-tidy                                 # lint rules enforcing the clean-code constraints
.gitignore / .dockerignore
Dockerfile                                  # toolchain + deps; builds + runs ctest
test/docker-compose.yml                     # `unit` service: build & test in a container
test/vm/Vagrantfile                         # disposable VM scaffold (exercised in Phase 4)
test/vm/snapshot.sh                         # snapshot / run / revert helpers
.github/workflows/ci.yml                    # build + unit tests + format + tidy

core/CMakeLists.txt
core/include/devmgr/core/version.hpp        # version() declaration
core/include/devmgr/core/result.hpp         # Error, Error::Code, Result<T>, makeError()
core/include/devmgr/core/models.hpp         # Device, Driver, enums, *State, Snapshot, HistoryGraph
core/include/devmgr/core/events.hpp         # Device*/Task*/Error domain event structs
core/include/devmgr/runtime/event_bus.hpp   # EventBus + Subscription (header-only templates)
core/include/devmgr/runtime/logging.hpp     # LogLevel, toSpdlogLevel(), init()
core/include/devmgr/runtime/cancellation.hpp# CancellationSource / CancellationToken
core/include/devmgr/runtime/progress.hpp    # ProgressUpdate, ProgressReporter
core/include/devmgr/runtime/task_scheduler.hpp
core/include/devmgr/pal/hotplug_event.hpp   # HotplugEvent
core/include/devmgr/pal/interfaces.hpp      # the 7 PAL interfaces (pure virtual)
core/src/version.cpp
core/src/models.cpp                         # to_string(enum) helpers, DeviceId ==
core/src/logging.cpp
core/src/task_scheduler.cpp

tests/CMakeLists.txt
tests/fakes/fake_pal.hpp                     # in-memory PAL test double
tests/unit/test_version.cpp
tests/unit/test_result.cpp
tests/unit/test_models.cpp
tests/unit/test_event_bus.cpp
tests/unit/test_logging.cpp
tests/unit/test_task_scheduler.cpp
tests/unit/test_fake_pal.cpp
```

---

### Task 1: Build scaffold + version smoke test

Establishes CMake + vcpkg + GoogleTest end-to-end via one trivial tested function. Folds in all config files.

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, `.clang-format`, `.clang-tidy`, `.gitignore`, `.dockerignore`
- Create: `core/CMakeLists.txt`, `core/include/devmgr/core/version.hpp`, `core/src/version.cpp`
- Create: `tests/CMakeLists.txt`, `tests/unit/test_version.cpp`

**Interfaces:**
- Produces: `devmgr::core::version() -> std::string_view` (returns `"0.1.0"`); CMake targets `devmgr_core` (static lib) and `devmgr_tests` (gtest exe).

- [ ] **Step 1: Write the failing test**

`tests/unit/test_version.cpp`:
```cpp
#include <gtest/gtest.h>
#include "devmgr/core/version.hpp"

TEST(Version, ReportsSemver) {
    EXPECT_EQ(devmgr::core::version(), "0.1.0");
}
```

- [ ] **Step 2: Create the build files**

`vcpkg.json`:
```json
{
  "name": "cross-device-manager",
  "version": "0.1.0",
  "dependencies": ["gtest", "spdlog", "nlohmann-json", "tl-expected"]
}
```

`CMakePresets.json`:
```json
{
  "version": 3,
  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
  "configurePresets": [
    {
      "name": "linux-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/linux-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    }
  ],
  "buildPresets": [{ "name": "linux-debug", "configurePreset": "linux-debug" }]
}
```

`CMakeLists.txt` (root):
```cmake
cmake_minimum_required(VERSION 3.21)
project(cross_device_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()
add_subdirectory(core)
add_subdirectory(tests)
```

`core/CMakeLists.txt`:
```cmake
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_library(devmgr_core
    src/version.cpp
)
target_include_directories(devmgr_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devmgr_core
    PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json tl::expected Threads::Threads)
target_compile_features(devmgr_core PUBLIC cxx_std_20)
```

`tests/CMakeLists.txt`:
```cmake
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_tests
    unit/test_version.cpp
)
target_link_libraries(devmgr_tests PRIVATE devmgr_core GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(devmgr_tests)
```

`.clang-format`:
```yaml
BasedOnStyle: Google
ColumnLimit: 100
IndentWidth: 4
DerivePointerAlignment: false
PointerAlignment: Left
```

`.clang-tidy`:
```yaml
Checks: >
  bugprone-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-function-cognitive-complexity,
  readability-function-size,
  -modernize-use-trailing-return-type
WarningsAsErrors: ''
CheckOptions:
  - { key: readability-function-size.StatementThreshold, value: '40' }
  - { key: readability-function-cognitive-complexity.Threshold, value: '15' }
```

`.gitignore`:
```
/build/
/vcpkg_installed/
compile_commands.json
```

`.dockerignore`:
```
build/
vcpkg_installed/
.git/
```

- [ ] **Step 3: Write the header**

`core/include/devmgr/core/version.hpp`:
```cpp
#pragma once
#include <string_view>

namespace devmgr::core {
std::string_view version();
}  // namespace devmgr::core
```

- [ ] **Step 4: Run test to verify it fails**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: **link error** — `undefined reference to devmgr::core::version()`.

- [ ] **Step 5: Write minimal implementation**

`core/src/version.cpp`:
```cpp
#include "devmgr/core/version.hpp"

namespace devmgr::core {
std::string_view version() { return "0.1.0"; }
}  // namespace devmgr::core
```

- [ ] **Step 6: Build and run the test**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — `Version.ReportsSemver`.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt CMakePresets.json vcpkg.json .clang-format .clang-tidy .gitignore .dockerignore core tests
git commit -m "build: scaffold CMake + vcpkg + gtest with version smoke test"
```

---

### Task 2: Result / Error type

**Files:**
- Create: `core/include/devmgr/core/result.hpp`
- Test: `tests/unit/test_result.cpp` (add source to `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `tl::expected` from the `tl-expected` package.
- Produces: `devmgr::core::Error`, `Error::Code{Permission,NotFound,Busy,Io,Network,Unsupported,Conflict}`, `Result<T> = tl::expected<T, Error>`, `makeError(Error::Code, std::string) -> tl::unexpected<Error>`.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_result.cpp`:
```cpp
#include <gtest/gtest.h>
#include "devmgr/core/result.hpp"

using devmgr::core::Error;
using devmgr::core::makeError;
using devmgr::core::Result;

Result<int> doubleIfPositive(int n) {
    if (n <= 0) return makeError(Error::Code::NotFound, "non-positive");
    return n * 2;
}

TEST(Result, CarriesValueOnSuccess) {
    auto r = doubleIfPositive(21);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(*r, 42);
}

TEST(Result, CarriesErrorOnFailure) {
    auto r = doubleIfPositive(-1);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(r.error().message, "non-positive");
}
```

- [ ] **Step 2: Register the test**

In `tests/CMakeLists.txt`, add `unit/test_result.cpp` to the `devmgr_tests` source list.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: compile error — `result.hpp` not found.

- [ ] **Step 4: Write the header**

`core/include/devmgr/core/result.hpp`:
```cpp
#pragma once
#include <string>
#include <utility>

#include <tl/expected.hpp>

namespace devmgr::core {

struct Error {
    enum class Code { Permission, NotFound, Busy, Io, Network, Unsupported, Conflict };
    Code code;
    std::string message;
};

template <class T>
using Result = tl::expected<T, Error>;

inline tl::unexpected<Error> makeError(Error::Code code, std::string message) {
    return tl::unexpected<Error>(Error{code, std::move(message)});
}

}  // namespace devmgr::core
```

- [ ] **Step 5: Build and run**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — both `Result.*` tests.

- [ ] **Step 6: Commit**

```bash
git add core/include/devmgr/core/result.hpp tests/unit/test_result.cpp tests/CMakeLists.txt
git commit -m "feat(core): add Result/Error type built on tl::expected"
```

---

### Task 3: Domain models + enum string helpers

**Files:**
- Create: `core/include/devmgr/core/models.hpp`, `core/src/models.cpp`
- Test: `tests/unit/test_models.cpp` (register in `tests/CMakeLists.txt`)
- Modify: `core/CMakeLists.txt` — add `src/models.cpp`

**Interfaces:**
- Produces: `DeviceId` (with `operator==` and `std::hash`), enums `DeviceStatus`, `BusType`, `DriverKind`; structs `Device`, `Driver`, `DeviceState`, `DriverState`, `Snapshot`, `HistoryGraph`; `to_string(DeviceStatus|BusType|DriverKind) -> const char*`.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_models.cpp`:
```cpp
#include <gtest/gtest.h>
#include <unordered_set>
#include "devmgr/core/models.hpp"

using namespace devmgr::core;

TEST(Models, StatusToString) {
    EXPECT_STREQ(to_string(DeviceStatus::Active), "Active");
    EXPECT_STREQ(to_string(DeviceStatus::Disabled), "Disabled");
    EXPECT_STREQ(to_string(DeviceStatus::Transitioning), "Transitioning");
    EXPECT_STREQ(to_string(DeviceStatus::Error), "Error");
    EXPECT_STREQ(to_string(DeviceStatus::Unknown), "Unknown");
}

TEST(Models, BusAndDriverKindToString) {
    EXPECT_STREQ(to_string(BusType::Pci), "Pci");
    EXPECT_STREQ(to_string(BusType::Usb), "Usb");
    EXPECT_STREQ(to_string(DriverKind::KernelModule), "KernelModule");
    EXPECT_STREQ(to_string(DriverKind::Firmware), "Firmware");
}

TEST(Models, DeviceIdIsHashableAndComparable) {
    DeviceId a{"pci:0000:00:1f.2"};
    DeviceId b{"pci:0000:00:1f.2"};
    DeviceId c{"usb:1-2"};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
    std::unordered_set<DeviceId> seen{a};
    EXPECT_EQ(seen.count(b), 1u);
}
```

- [ ] **Step 2: Register the test + source**

Add `unit/test_models.cpp` to `tests/CMakeLists.txt`, and `src/models.cpp` to `core/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: compile error — `models.hpp` not found.

- [ ] **Step 4: Write the header**

`core/include/devmgr/core/models.hpp`:
```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace devmgr::core {

struct DeviceId {
    std::string value;
};
inline bool operator==(const DeviceId& a, const DeviceId& b) { return a.value == b.value; }

enum class DeviceStatus { Active, Disabled, Transitioning, Error, Unknown };
enum class BusType { Pci, Usb, Platform, Virtio, Other };
enum class DriverKind { KernelModule, Firmware, Package, Builtin };

const char* to_string(DeviceStatus status);
const char* to_string(BusType bus);
const char* to_string(DriverKind kind);

struct Device {
    DeviceId id;
    BusType bus = BusType::Other;
    std::string name;
    std::string sysfsPath;
    std::string modalias;
    std::string vendorId;
    std::string productId;
    std::string serial;
    std::optional<DeviceId> parent;
    DeviceStatus status = DeviceStatus::Unknown;
    std::optional<std::string> boundDriver;
    std::map<std::string, std::string> properties;
    std::optional<std::string> errorNote;
};

struct Driver {
    std::string name;
    DriverKind kind = DriverKind::KernelModule;
    std::string version;
    std::string path;
    bool loaded = false;
    bool isSigned = false;
    std::optional<std::string> availableUpdate;
    std::vector<std::string> dependencies;
};

struct DeviceState {
    DeviceId id;
    bool enabled = true;
    std::optional<std::string> boundDriver;
    std::optional<std::string> driverVersion;
};

struct DriverState {
    std::string name;
    std::string version;
    bool loaded = false;
    std::vector<std::string> options;
};

struct Snapshot {
    std::string id;  // sha256 of canonical body (filled by BackupService in Phase 7)
    std::optional<std::string> parent;
    std::int64_t timestampUtc = 0;
    std::string author;
    std::string description;
    std::string osVersion;
    std::string kernelVersion;
    std::vector<DeviceState> devices;
    std::vector<DriverState> drivers;
    std::vector<std::string> modprobeConfigDigests;
};

struct HistoryGraph {
    std::string head;
    std::map<std::string, Snapshot> nodes;
};

}  // namespace devmgr::core

template <>
struct std::hash<devmgr::core::DeviceId> {
    std::size_t operator()(const devmgr::core::DeviceId& id) const noexcept {
        return std::hash<std::string>{}(id.value);
    }
};
```

- [ ] **Step 5: Write the implementation**

`core/src/models.cpp`:
```cpp
#include "devmgr/core/models.hpp"

namespace devmgr::core {

const char* to_string(DeviceStatus status) {
    switch (status) {
        case DeviceStatus::Active: return "Active";
        case DeviceStatus::Disabled: return "Disabled";
        case DeviceStatus::Transitioning: return "Transitioning";
        case DeviceStatus::Error: return "Error";
        case DeviceStatus::Unknown: return "Unknown";
    }
    return "Unknown";
}

const char* to_string(BusType bus) {
    switch (bus) {
        case BusType::Pci: return "Pci";
        case BusType::Usb: return "Usb";
        case BusType::Platform: return "Platform";
        case BusType::Virtio: return "Virtio";
        case BusType::Other: return "Other";
    }
    return "Other";
}

const char* to_string(DriverKind kind) {
    switch (kind) {
        case DriverKind::KernelModule: return "KernelModule";
        case DriverKind::Firmware: return "Firmware";
        case DriverKind::Package: return "Package";
        case DriverKind::Builtin: return "Builtin";
    }
    return "KernelModule";
}

}  // namespace devmgr::core
```

- [ ] **Step 6: Build and run**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — all three `Models.*` tests.

- [ ] **Step 7: Commit**

```bash
git add core/include/devmgr/core/models.hpp core/src/models.cpp core/CMakeLists.txt tests/unit/test_models.cpp tests/CMakeLists.txt
git commit -m "feat(core): add domain models and enum string helpers"
```

---

### Task 4: Event types + thread-safe EventBus

**Files:**
- Create: `core/include/devmgr/core/events.hpp`, `core/include/devmgr/runtime/event_bus.hpp`
- Test: `tests/unit/test_event_bus.cpp` (register in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `Device`, `DeviceId` from `models.hpp`.
- Produces: event structs `DeviceAddedEvent{Device}`, `DeviceRemovedEvent{DeviceId}`, `DeviceChangedEvent{Device}`, `TaskProgressEvent{std::string taskId; int percent; std::string stage}`, `TaskCompletedEvent{std::string taskId; bool ok; std::string message}`, `ErrorEvent{std::string source; std::string message}`. `EventBus` with `template<class E> Subscription subscribe(std::function<void(const E&)>)` and `template<class E> void publish(const E&)`. `Subscription` is a move-only RAII token that unsubscribes on destruction.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_event_bus.cpp`:
```cpp
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include "devmgr/core/events.hpp"
#include "devmgr/runtime/event_bus.hpp"

using devmgr::core::DeviceAddedEvent;
using devmgr::runtime::EventBus;
using devmgr::runtime::Subscription;

TEST(EventBus, DeliversEventToSubscriber) {
    EventBus bus;
    int count = 0;
    auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(count, 1);
}

TEST(EventBus, StopsDeliveryAfterUnsubscribe) {
    EventBus bus;
    int count = 0;
    {
        auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
        bus.publish(DeviceAddedEvent{});
    }  // sub destroyed -> unsubscribed
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(count, 1);
}

TEST(EventBus, DeliversToAllSubscribers) {
    EventBus bus;
    int a = 0, b = 0;
    auto s1 = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++a; });
    auto s2 = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++b; });
    bus.publish(DeviceAddedEvent{});
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(EventBus, IsThreadSafeUnderConcurrentPublish) {
    EventBus bus;
    std::atomic<int> count{0};
    auto sub = bus.subscribe<DeviceAddedEvent>([&](const DeviceAddedEvent&) { ++count; });
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < 250; ++i) bus.publish(DeviceAddedEvent{});
        });
    for (auto& th : threads) th.join();
    EXPECT_EQ(count.load(), 1000);
}
```

- [ ] **Step 2: Register the test**

Add `unit/test_event_bus.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: compile error — `events.hpp` / `event_bus.hpp` not found.

- [ ] **Step 4: Write the event types**

`core/include/devmgr/core/events.hpp`:
```cpp
#pragma once
#include <string>
#include "devmgr/core/models.hpp"

namespace devmgr::core {

struct DeviceAddedEvent { Device device; };
struct DeviceRemovedEvent { DeviceId id; };
struct DeviceChangedEvent { Device device; };
struct TaskProgressEvent { std::string taskId; int percent = 0; std::string stage; };
struct TaskCompletedEvent { std::string taskId; bool ok = false; std::string message; };
struct ErrorEvent { std::string source; std::string message; };

}  // namespace devmgr::core
```

- [ ] **Step 5: Write the EventBus (header-only)**

`core/include/devmgr/runtime/event_bus.hpp`:
```cpp
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace devmgr::runtime {

class EventBus;

// Move-only RAII token; unsubscribes its handler on destruction.
class Subscription {
public:
    Subscription() = default;
    Subscription(EventBus* bus, std::type_index type, std::uint64_t id)
        : bus_(bus), type_(type), id_(id) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept { moveFrom(other); }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            moveFrom(other);
        }
        return *this;
    }
    ~Subscription() { reset(); }

    void reset();

private:
    void moveFrom(Subscription& other) {
        bus_ = other.bus_;
        type_ = other.type_;
        id_ = other.id_;
        other.bus_ = nullptr;
    }

    EventBus* bus_ = nullptr;
    std::type_index type_ = std::type_index(typeid(void));
    std::uint64_t id_ = 0;
};

class EventBus {
public:
    template <class Event>
    [[nodiscard]] Subscription subscribe(std::function<void(const Event&)> handler) {
        const auto type = std::type_index(typeid(Event));
        auto stored = std::make_shared<std::function<void(const Event&)>>(std::move(handler));
        std::lock_guard<std::mutex> lock(mutex_);
        const auto id = nextId_++;
        handlers_[type].push_back(Entry{id, std::move(stored)});
        return Subscription(this, type, id);
    }

    template <class Event>
    void publish(const Event& event) {
        std::vector<std::shared_ptr<void>> targets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = handlers_.find(std::type_index(typeid(Event)));
            if (it == handlers_.end()) return;
            targets.reserve(it->second.size());
            for (const auto& entry : it->second) targets.push_back(entry.fn);
        }
        // Invoke outside the lock so handlers may (un)subscribe without deadlock.
        for (const auto& fn : targets) {
            (*std::static_pointer_cast<std::function<void(const Event&)>>(fn))(event);
        }
    }

private:
    friend class Subscription;
    struct Entry {
        std::uint64_t id;
        std::shared_ptr<void> fn;
    };

    void unsubscribe(std::type_index type, std::uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = handlers_.find(type);
        if (it == handlers_.end()) return;
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(),
                                 [id](const Entry& e) { return e.id == id; }),
                  vec.end());
    }

    std::mutex mutex_;
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::type_index, std::vector<Entry>> handlers_;
};

inline void Subscription::reset() {
    if (bus_ == nullptr) return;
    bus_->unsubscribe(type_, id_);
    bus_ = nullptr;
}

}  // namespace devmgr::runtime
```

- [ ] **Step 6: Build and run**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — all four `EventBus.*` tests (run repeatedly to shake out races if desired: `ctest --test-dir build/linux-debug -R EventBus --repeat until-fail:20`).

- [ ] **Step 7: Commit**

```bash
git add core/include/devmgr/core/events.hpp core/include/devmgr/runtime/event_bus.hpp tests/unit/test_event_bus.cpp tests/CMakeLists.txt
git commit -m "feat(runtime): add domain events and thread-safe EventBus"
```

---

### Task 5: Logging facade

A thin, testable wrapper over spdlog. The unit-tested behavior is the pure `LogLevel -> spdlog level` mapping; `init()` wires the default logger.

**Files:**
- Create: `core/include/devmgr/runtime/logging.hpp`, `core/src/logging.cpp`
- Test: `tests/unit/test_logging.cpp` (register in `tests/CMakeLists.txt`)
- Modify: `core/CMakeLists.txt` — add `src/logging.cpp`

**Interfaces:**
- Produces: `enum class LogLevel{Trace,Debug,Info,Warn,Error}`, `spdlog::level::level_enum toSpdlogLevel(LogLevel)`, `void init(LogLevel level = LogLevel::Info)`.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_logging.cpp`:
```cpp
#include <gtest/gtest.h>
#include "devmgr/runtime/logging.hpp"

using devmgr::runtime::LogLevel;
using devmgr::runtime::toSpdlogLevel;

TEST(Logging, MapsLevelsToSpdlog) {
    EXPECT_EQ(toSpdlogLevel(LogLevel::Trace), spdlog::level::trace);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Debug), spdlog::level::debug);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Info), spdlog::level::info);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Warn), spdlog::level::warn);
    EXPECT_EQ(toSpdlogLevel(LogLevel::Error), spdlog::level::err);
}

TEST(Logging, InitSetsDefaultLoggerLevel) {
    devmgr::runtime::init(LogLevel::Warn);
    EXPECT_EQ(spdlog::default_logger()->level(), spdlog::level::warn);
}
```

- [ ] **Step 2: Register the test + source**

Add `unit/test_logging.cpp` to `tests/CMakeLists.txt` and `src/logging.cpp` to `core/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: compile error — `logging.hpp` not found.

- [ ] **Step 4: Write the header**

`core/include/devmgr/runtime/logging.hpp`:
```cpp
#pragma once
#include <spdlog/spdlog.h>

namespace devmgr::runtime {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

spdlog::level::level_enum toSpdlogLevel(LogLevel level);
void init(LogLevel level = LogLevel::Info);

}  // namespace devmgr::runtime
```

- [ ] **Step 5: Write the implementation**

`core/src/logging.cpp`:
```cpp
#include "devmgr/runtime/logging.hpp"

namespace devmgr::runtime {

spdlog::level::level_enum toSpdlogLevel(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return spdlog::level::trace;
        case LogLevel::Debug: return spdlog::level::debug;
        case LogLevel::Info: return spdlog::level::info;
        case LogLevel::Warn: return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
    }
    return spdlog::level::info;
}

void init(LogLevel level) {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    spdlog::set_level(toSpdlogLevel(level));
}

}  // namespace devmgr::runtime
```

- [ ] **Step 6: Build and run**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — both `Logging.*` tests.

- [ ] **Step 7: Commit**

```bash
git add core/include/devmgr/runtime/logging.hpp core/src/logging.cpp core/CMakeLists.txt tests/unit/test_logging.cpp tests/CMakeLists.txt
git commit -m "feat(runtime): add spdlog-backed logging facade"
```

---

### Task 6: Cancellation + TaskScheduler + progress

**Files:**
- Create: `core/include/devmgr/runtime/cancellation.hpp`, `core/include/devmgr/runtime/progress.hpp`, `core/include/devmgr/runtime/task_scheduler.hpp`, `core/src/task_scheduler.cpp`
- Test: `tests/unit/test_task_scheduler.cpp` (register in `tests/CMakeLists.txt`)
- Modify: `core/CMakeLists.txt` — add `src/task_scheduler.cpp`

**Interfaces:**
- Produces:
  - `CancellationSource` with `token() -> CancellationToken`, `cancel()`, `isCancelled() -> bool`.
  - `CancellationToken` with `isCancellationRequested() -> bool`.
  - `struct ProgressUpdate{int percent; std::string stage;}`, `using ProgressReporter = std::function<void(const ProgressUpdate&)>`.
  - `TaskScheduler(std::size_t threadCount = defaultThreadCount())`, `template<class F, class... Args> submit(F&&, Args&&...) -> std::future<...>`, `threadCount() -> std::size_t`, `static defaultThreadCount() -> std::size_t`. Destructor drains queued work and joins.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_task_scheduler.cpp`:
```cpp
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include "devmgr/runtime/cancellation.hpp"
#include "devmgr/runtime/progress.hpp"
#include "devmgr/runtime/task_scheduler.hpp"

using devmgr::runtime::CancellationSource;
using devmgr::runtime::ProgressReporter;
using devmgr::runtime::ProgressUpdate;
using devmgr::runtime::TaskScheduler;

TEST(TaskScheduler, RunsSubmittedWorkAndReturnsValue) {
    TaskScheduler pool{2};
    auto future = pool.submit([] { return 6 * 7; });
    EXPECT_EQ(future.get(), 42);
}

TEST(TaskScheduler, RunsManyTasks) {
    TaskScheduler pool{4};
    std::atomic<int> sum{0};
    std::vector<std::future<void>> futures;
    for (int i = 1; i <= 100; ++i)
        futures.push_back(pool.submit([&sum, i] { sum += i; }));
    for (auto& f : futures) f.get();
    EXPECT_EQ(sum.load(), 5050);
}

TEST(Cancellation, TokenObservesRequest) {
    CancellationSource source;
    auto token = source.token();
    EXPECT_FALSE(token.isCancellationRequested());
    source.cancel();
    EXPECT_TRUE(token.isCancellationRequested());
}

TEST(Progress, ReporterForwardsUpdates) {
    ProgressUpdate seen{};
    ProgressReporter reporter = [&](const ProgressUpdate& u) { seen = u; };
    reporter(ProgressUpdate{50, "downloading"});
    EXPECT_EQ(seen.percent, 50);
    EXPECT_EQ(seen.stage, "downloading");
}
```

- [ ] **Step 2: Register the test + source**

Add `unit/test_task_scheduler.cpp` to `tests/CMakeLists.txt` and `src/task_scheduler.cpp` to `core/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: compile error — runtime headers not found.

- [ ] **Step 4: Write the headers**

`core/include/devmgr/runtime/cancellation.hpp`:
```cpp
#pragma once
#include <atomic>
#include <memory>

namespace devmgr::runtime {

class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic_bool>(false)) {}
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag)
        : flag_(std::move(flag)) {}
    bool isCancellationRequested() const { return flag_ && flag_->load(); }

private:
    std::shared_ptr<std::atomic_bool> flag_;
};

class CancellationSource {
public:
    CancellationSource() : flag_(std::make_shared<std::atomic_bool>(false)) {}
    CancellationToken token() const { return CancellationToken(flag_); }
    void cancel() { flag_->store(true); }
    bool isCancelled() const { return flag_->load(); }

private:
    std::shared_ptr<std::atomic_bool> flag_;
};

}  // namespace devmgr::runtime
```

`core/include/devmgr/runtime/progress.hpp`:
```cpp
#pragma once
#include <functional>
#include <string>

namespace devmgr::runtime {

struct ProgressUpdate {
    int percent = 0;
    std::string stage;
};

using ProgressReporter = std::function<void(const ProgressUpdate&)>;

}  // namespace devmgr::runtime
```

`core/include/devmgr/runtime/task_scheduler.hpp`:
```cpp
#pragma once
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace devmgr::runtime {

// Fixed-size thread pool. Destructor drains queued work then joins workers.
class TaskScheduler {
public:
    explicit TaskScheduler(std::size_t threadCount = defaultThreadCount());
    ~TaskScheduler();

    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;

    template <class F, class... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using Return = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<Return()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        std::future<Return> future = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) throw std::runtime_error("TaskScheduler is stopping");
            queue_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    std::size_t threadCount() const { return workers_.size(); }
    static std::size_t defaultThreadCount();

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_ = false;
};

}  // namespace devmgr::runtime
```

- [ ] **Step 5: Write the implementation**

`core/src/task_scheduler.cpp`:
```cpp
#include "devmgr/runtime/task_scheduler.hpp"

#include <algorithm>

namespace devmgr::runtime {

std::size_t TaskScheduler::defaultThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware == 0) return 2;
    return std::max(2u, hardware);
}

TaskScheduler::TaskScheduler(std::size_t threadCount) {
    if (threadCount == 0) threadCount = 1;
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
        workers_.emplace_back([this] { workerLoop(); });
}

TaskScheduler::~TaskScheduler() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
}

void TaskScheduler::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.pop();
        }
        job();
    }
}

}  // namespace devmgr::runtime
```

- [ ] **Step 6: Build and run**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — all `TaskScheduler.*`, `Cancellation.*`, `Progress.*` tests.

- [ ] **Step 7: Commit**

```bash
git add core/include/devmgr/runtime/cancellation.hpp core/include/devmgr/runtime/progress.hpp core/include/devmgr/runtime/task_scheduler.hpp core/src/task_scheduler.cpp core/CMakeLists.txt tests/unit/test_task_scheduler.cpp tests/CMakeLists.txt
git commit -m "feat(runtime): add cancellation, progress, and TaskScheduler thread pool"
```

---

### Task 7: PAL interfaces + in-memory FakePal

Declares the seven pure-virtual PAL contracts and provides an in-memory `FakePal` test double (implementing the read/control interfaces) that later phases and core-service tests use instead of real OS access.

**Files:**
- Create: `core/include/devmgr/pal/hotplug_event.hpp`, `core/include/devmgr/pal/interfaces.hpp`
- Create: `tests/fakes/fake_pal.hpp`
- Test: `tests/unit/test_fake_pal.cpp` (register in `tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `Device`, `Driver`, `DeviceId`, `Result<T>`, `ProgressReporter`.
- Produces:
  - `struct HotplugEvent { enum class Action{Added,Removed,Changed}; Action action; Device device; };`
  - Interfaces: `IDeviceEnumerator{ Result<std::vector<Device>> enumerate() }`, `IHotplugMonitor{ Result<void> start(std::function<void(const HotplugEvent&)>); void stop() }`, `IDeviceController{ Result<void> setEnabled(const DeviceId&, bool) }`, `IDriverManager{ Result<std::vector<Driver>> driversFor(const DeviceId&); Result<void> loadModule(const std::string&); Result<void> unloadModule(const std::string&) }`, `IUpdateProvider{ Result<std::vector<Driver>> checkUpdates(); Result<void> applyUpdate(const std::string&, ProgressReporter) }`, `ISystemInfo{ struct Info{std::string osVersion,kernelVersion; bool secureBoot,rebootPending;}; Result<Info> query() }`, `IPrivilegedChannel{ Result<void> setDeviceEnabled(const DeviceId&, bool) }`.
  - `FakePal` implementing `IDeviceEnumerator`, `IDeviceController`, `IDriverManager`, `ISystemInfo`, with `seedDevice(Device)` / `seedDriver(DeviceId, Driver)` setters and an `enabled(DeviceId) -> bool` query.

- [ ] **Step 1: Write the failing test**

`tests/unit/test_fake_pal.cpp`:
```cpp
#include <gtest/gtest.h>
#include "devmgr/pal/interfaces.hpp"
#include "fakes/fake_pal.hpp"

using devmgr::core::Device;
using devmgr::core::DeviceId;
using devmgr::core::DeviceStatus;
using devmgr::pal::IDeviceController;
using devmgr::pal::IDeviceEnumerator;
using devmgr::test::FakePal;

TEST(FakePal, EnumeratesSeededDevices) {
    FakePal pal;
    pal.seedDevice(Device{DeviceId{"usb:1-2"}});
    IDeviceEnumerator& enumerator = pal;
    auto result = enumerator.enumerate();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->size(), 1u);
    EXPECT_EQ((*result)[0].id.value, "usb:1-2");
}

TEST(FakePal, SetEnabledUpdatesState) {
    FakePal pal;
    const DeviceId id{"usb:1-2"};
    pal.seedDevice(Device{id});
    IDeviceController& controller = pal;
    ASSERT_TRUE(controller.setEnabled(id, false).has_value());
    EXPECT_FALSE(pal.enabled(id));
}

TEST(FakePal, SetEnabledOnUnknownDeviceReturnsNotFound) {
    FakePal pal;
    IDeviceController& controller = pal;
    auto result = controller.setEnabled(DeviceId{"ghost"}, false);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, devmgr::core::Error::Code::NotFound);
}
```

- [ ] **Step 2: Register the test**

Add `unit/test_fake_pal.cpp` to `tests/CMakeLists.txt`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: compile error — `interfaces.hpp` / `fake_pal.hpp` not found.

- [ ] **Step 4: Write the PAL headers**

`core/include/devmgr/pal/hotplug_event.hpp`:
```cpp
#pragma once
#include "devmgr/core/models.hpp"

namespace devmgr::pal {

struct HotplugEvent {
    enum class Action { Added, Removed, Changed };
    Action action;
    core::Device device;
};

}  // namespace devmgr::pal
```

`core/include/devmgr/pal/interfaces.hpp`:
```cpp
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/progress.hpp"

namespace devmgr::pal {

class IDeviceEnumerator {
public:
    virtual ~IDeviceEnumerator() = default;
    virtual core::Result<std::vector<core::Device>> enumerate() = 0;
};

class IHotplugMonitor {
public:
    using Callback = std::function<void(const HotplugEvent&)>;
    virtual ~IHotplugMonitor() = default;
    virtual core::Result<void> start(Callback callback) = 0;
    virtual void stop() = 0;
};

class IDeviceController {
public:
    virtual ~IDeviceController() = default;
    virtual core::Result<void> setEnabled(const core::DeviceId& id, bool enabled) = 0;
};

class IDriverManager {
public:
    virtual ~IDriverManager() = default;
    virtual core::Result<std::vector<core::Driver>> driversFor(const core::DeviceId& id) = 0;
    virtual core::Result<void> loadModule(const std::string& name) = 0;
    virtual core::Result<void> unloadModule(const std::string& name) = 0;
};

class IUpdateProvider {
public:
    virtual ~IUpdateProvider() = default;
    virtual core::Result<std::vector<core::Driver>> checkUpdates() = 0;
    virtual core::Result<void> applyUpdate(const std::string& id,
                                           runtime::ProgressReporter reporter) = 0;
};

class ISystemInfo {
public:
    struct Info {
        std::string osVersion;
        std::string kernelVersion;
        bool secureBoot = false;
        bool rebootPending = false;
    };
    virtual ~ISystemInfo() = default;
    virtual core::Result<Info> query() = 0;
};

class IPrivilegedChannel {
public:
    virtual ~IPrivilegedChannel() = default;
    virtual core::Result<void> setDeviceEnabled(const core::DeviceId& id, bool enabled) = 0;
};

}  // namespace devmgr::pal
```

- [ ] **Step 5: Write the FakePal test double**

`tests/fakes/fake_pal.hpp`:
```cpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "devmgr/pal/interfaces.hpp"

namespace devmgr::test {

// In-memory PAL double for unit tests. Implements the read/control interfaces.
class FakePal final : public pal::IDeviceEnumerator,
                      public pal::IDeviceController,
                      public pal::IDriverManager,
                      public pal::ISystemInfo {
public:
    void seedDevice(core::Device device) {
        const auto id = device.id;
        enabled_[id.value] = true;
        devices_.push_back(std::move(device));
    }
    void seedDriver(const core::DeviceId& id, core::Driver driver) {
        drivers_[id.value].push_back(std::move(driver));
    }
    bool enabled(const core::DeviceId& id) const {
        const auto it = enabled_.find(id.value);
        return it != enabled_.end() && it->second;
    }

    core::Result<std::vector<core::Device>> enumerate() override { return devices_; }

    core::Result<void> setEnabled(const core::DeviceId& id, bool enabled) override {
        const auto it = enabled_.find(id.value);
        if (it == enabled_.end())
            return core::makeError(core::Error::Code::NotFound, "no such device: " + id.value);
        it->second = enabled;
        return {};
    }

    core::Result<std::vector<core::Driver>> driversFor(const core::DeviceId& id) override {
        const auto it = drivers_.find(id.value);
        if (it == drivers_.end()) return std::vector<core::Driver>{};
        return it->second;
    }
    core::Result<void> loadModule(const std::string&) override { return {}; }
    core::Result<void> unloadModule(const std::string&) override { return {}; }

    core::Result<Info> query() override {
        return Info{"fake-os 1.0", "6.1.0-fake", false, false};
    }

private:
    std::vector<core::Device> devices_;
    std::unordered_map<std::string, bool> enabled_;
    std::unordered_map<std::string, std::vector<core::Driver>> drivers_;
};

}  // namespace devmgr::test
```

- [ ] **Step 6: Build and run**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: PASS — all three `FakePal.*` tests.

- [ ] **Step 7: Commit**

```bash
git add core/include/devmgr/pal tests/fakes/fake_pal.hpp tests/unit/test_fake_pal.cpp tests/CMakeLists.txt
git commit -m "feat(pal): add PAL interfaces and in-memory FakePal test double"
```

---

### Task 8: Docker dev/test image

A container that installs the toolchain + system deps, bootstraps vcpkg, builds, and runs the unit suite — the safe (userspace-only) CI surface. (Real `umockdev` integration tests arrive in Phase 1 when there's a udev enumerator to exercise; this task wires the image they'll run in.)

**Files:**
- Create: `Dockerfile`, `test/docker-compose.yml`

**Interfaces:**
- Produces: a `unit` compose service that exits non-zero if any test fails.

- [ ] **Step 1: Write the Dockerfile**

`Dockerfile`:
```dockerfile
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive VCPKG_ROOT=/opt/vcpkg
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config \
        clang-tidy clang-format ca-certificates \
        libudev-dev libkmod-dev umockdev libumockdev-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY . .
RUN cmake --preset linux-debug && cmake --build --preset linux-debug
CMD ["ctest", "--test-dir", "build/linux-debug", "--output-on-failure"]
```

- [ ] **Step 2: Write the compose file**

`test/docker-compose.yml`:
```yaml
services:
  unit:
    build:
      context: ..
      dockerfile: Dockerfile
    command: ["ctest", "--test-dir", "build/linux-debug", "--output-on-failure"]
```

- [ ] **Step 3: Build the image (this is the test)**

Run: `docker compose -f test/docker-compose.yml build unit`
Expected: image builds; the in-image `cmake --build` succeeds.

- [ ] **Step 4: Run the unit suite in the container**

Run: `docker compose -f test/docker-compose.yml run --rm unit`
Expected: all tests PASS; container exits 0.

- [ ] **Step 5: Commit**

```bash
git add Dockerfile test/docker-compose.yml
git commit -m "build: add Docker dev/test image running the unit suite"
```

---

### Task 9: Disposable VM scaffold + CI workflow

Scaffolds the snapshot-revert VM used in Phase 4+ for kernel-level tests, and the CI pipeline (format + tidy + Dockerized unit tests). The VM is not exercised here (no dangerous ops exist yet) — it is validated structurally.

**Files:**
- Create: `test/vm/Vagrantfile`, `test/vm/snapshot.sh`, `.github/workflows/ci.yml`

**Interfaces:**
- Produces: `test/vm/snapshot.sh {create|run|revert}` helper; a CI workflow that gates merges on format, tidy, and unit tests.

- [ ] **Step 1: Write the VM scaffold**

`test/vm/Vagrantfile`:
```ruby
# Disposable VM for kernel-level tests (Phase 4+): real module load, bind/unbind.
# Snapshot before a dangerous run, revert after. Not used by Phase 0 tests.
Vagrant.configure("2") do |config|
  config.vm.box = "generic/ubuntu2404"
  config.vm.hostname = "devmgr-test"
  config.vm.provider "libvirt" do |lv|
    lv.memory = 2048
    lv.cpus = 2
  end
  config.vm.provision "shell", inline: <<-SHELL
    apt-get update
    apt-get install -y build-essential cmake ninja-build pkg-config libudev-dev libkmod-dev
  SHELL
end
```

`test/vm/snapshot.sh`:
```bash
#!/usr/bin/env bash
# Snapshot / run / revert helper around the disposable test VM.
set -euo pipefail

SNAPSHOT_NAME="devmgr-clean"

usage() { echo "usage: $0 {create|run <cmd>|revert}" >&2; exit 2; }

case "${1:-}" in
    create) vagrant snapshot save "$SNAPSHOT_NAME" ;;
    run)    shift; vagrant ssh -c "${*:?command required}" ;;
    revert) vagrant snapshot restore "$SNAPSHOT_NAME" ;;
    *)      usage ;;
esac
```

- [ ] **Step 2: Write the CI workflow**

`.github/workflows/ci.yml`:
```yaml
name: ci
on:
  push:
  pull_request:

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Check formatting
        run: |
          sudo apt-get update && sudo apt-get install -y clang-format
          find core tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror
      - name: Build and unit-test (Docker)
        run: docker compose -f test/docker-compose.yml run --rm unit
```

- [ ] **Step 3: Validate the scaffolding (this is the test)**

Run:
```bash
chmod +x test/vm/snapshot.sh
bash -n test/vm/snapshot.sh        # syntax check; expect no output, exit 0
shellcheck test/vm/snapshot.sh || true   # lint if available
test/vm/snapshot.sh                # expect usage message + exit 2
```
Expected: `bash -n` passes; running with no args prints usage and exits 2.

- [ ] **Step 4: Validate the workflow YAML**

Run: `python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml')); print('ok')"`
Expected: prints `ok`.

- [ ] **Step 5: Commit**

```bash
git add test/vm/Vagrantfile test/vm/snapshot.sh .github/workflows/ci.yml
git commit -m "ci: add disposable VM scaffold and CI workflow (format + dockerized tests)"
```

---

## Self-Review

**1. Spec coverage (Phase 0 scope from the architecture plan):**

| Phase 0 deliverable | Task |
|---|---|
| Repo scaffold, CMake + vcpkg | Task 1 |
| CI | Task 9 |
| clang-tidy / clang-format | Task 1 (config) + Task 9 (enforced) |
| Core types (`Result`, `Device`, `Driver`, events) | Tasks 2, 3, 4 |
| `EventBus` | Task 4 |
| `TaskScheduler` (+ cancellation, progress) | Task 6 |
| `spdlog` logging | Task 5 |
| Mocked PAL + GoogleTest harness | Tasks 1 (harness), 7 (PAL + FakePal) |
| umockdev Docker rig | Task 8 (image; real umockdev tests deferred to Phase 1, noted) |
| Scripted disposable VM | Task 9 |

No Phase 0 deliverable is unaddressed. `Snapshot`/`HistoryGraph`/`IUpdateProvider`/`IHotplugMonitor`/`IPrivilegedChannel` are declared now (cheap, referenced by the architecture) but only fully fleshed in their later phases — intentional, not a gap.

**2. Placeholder scan:** No `TBD`/`TODO`/"add error handling"/"similar to Task N". Every code step shows complete content. The only deferral is real umockdev *integration tests* (Task 8 note), which require the Phase 1 udev enumerator to test against — correctly out of Phase 0 scope.

**3. Type consistency:** `Result<T>`, `Error::Code` (Task 2) are used identically in Tasks 7's FakePal and interfaces. `Device`/`Driver`/`DeviceId` field names (Task 3) match their use in events (Task 4) and FakePal (Task 7). `ProgressReporter`/`ProgressUpdate` (Task 6) match `IUpdateProvider::applyUpdate` (Task 7). `Subscription` move-only token (Task 4) used as designed. `to_string` overloads consistent. No naming drift found.

---

## Verification (whole phase)

1. **Local:** `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure` → all suites green.
2. **Container parity:** `docker compose -f test/docker-compose.yml run --rm unit` → exits 0.
3. **Lint/format clean:** `find core tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror` and `clang-tidy -p build/linux-debug core/src/*.cpp` → no errors.
4. **Race shakeout:** `ctest --test-dir build/linux-debug -R 'EventBus|TaskScheduler' --repeat until-fail:20` → stays green.
5. **Deliverable check:** `libdevmgr_core.a` exists under `build/linux-debug/core/`, exporting the domain types, `EventBus`, `TaskScheduler`, logging, and the PAL interfaces — ready for Phase 1's `UdevDeviceEnumerator`.
