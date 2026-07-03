# Phase 3 — Qt 6 GUI (read + hotplug parity) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Qt 6 Widgets GUI (`devmgr-gui`) showing the same device data and live hotplug as the TUI, reusing `devmgr_app` unchanged except two toolkit-agnostic `DeviceListVM` additions — proving the one-core/two-frontends abstraction.

**Architecture:** All new code lives in `gui/` (mirrors `tui/`): a `QtUiDispatcher` implementing `IUiDispatcher` via the auto-connection functor overload of `QMetaObject::invokeMethod`, a flat `DeviceListModel : QAbstractListModel` over the shared `DeviceListVM` (rows stay the single source of truth; every VM rebuild is bracketed by `beginResetModel()`/`endResetModel()` via new VM hooks), and a `MainWindow` mirroring the TUI layout. The composition root `runGuiApp()` replicates `runTuiApp()`'s construction/teardown ordering exactly.

**Tech Stack:** C++20, Qt 6 Widgets + Qt 6 Test (system packages: `pacman qt6-base` locally, `apt qt6-base-dev` in CI/Docker), GTest (existing framework — Qt tests are GTest-harnessed with a `QApplication` main, using `QAbstractItemModelTester` from Qt6::Test), CMake ≥ 3.21 with the existing `linux-debug` preset.

**Spec:** `docs/superpowers/specs/2026-06-29-phase3-qt-gui-design.md` (approved 2026-07-02).

## Resume State (2026-07-02, session end)

id|status|task|note
T1|x|VM isHeader + rebuild hooks|committed `86238c2`; 56/56
T2|x|CMake gating + QtUiDispatcher + offscreen harness|committed `345fef6`; 60/60
T3|x|DeviceListModel|committed `faa1a0d`; 64/64
T4|x|MainWindow|IMPL DONE, awaits user commit; 69/69, format clean
T5|.|runGuiApp + --self-test|next; 70 tests
T6|.|CI/Docker/purity guard|then user manual parity smoke (§ below) = phase exit gate

Next: USER commits T4 → T5 Step 1 (write `gui/src/gui_app.hpp/.cpp` + `main.cpp`, code verbatim in task; no new unit test — selftest smoke is the cycle). Skill flow: superpowers:executing-plans, inline, caveman docs.

Env gotchas (full detail: memory `phase3-execution-status`):
- Host clang-tidy (LLVM 21) dies on `-mno-direct-extern-access` — GCC-only flag from host Qt (`/usr/lib64/cmake/Qt6/Qt6Targets.cmake`) ∈ compile_commands.json ∀ gui/ TU. CI container unaffected. ⊥ strip from build. Local tidy: `sed 's/-mno-direct-extern-access //g' build/linux-debug/compile_commands.json > <dir>/compile_commands.json && clang-tidy -p <dir> --warnings-as-errors='*' <files>`. ! again @ T6 Step 4.
- "Verify it fails" steps → CMake generate error on missing `.cpp` (add_library ref), not plan's predicted compile error — same failing state, expected ∀ remaining create-file tasks.
- clangd flags fresh gui/ files not-found pre-configure — stale, ignore. `rtk` hook mangles some grep output — prefer direct commands / Read.

## Global Constraints

- **Qt floor is 6.4** (Ubuntu 24.04 CI). Never use the functor + `Qt::ConnectionType` overload of `QMetaObject::invokeMethod` — it is Qt 6.7+. Use the auto-connection functor overload (`invokeMethod(context, functor)`).
- **No Qt include may appear under `core/`, `app/`, or `platform/`** — Task 6 adds a CI grep gate; violating it mid-task means the seam is wrong, stop and re-design.
- **The user commits every task; the agent is denied `git add`/`git commit`.** Each task's final step hands the user a suggested commit message and waits.
- **Existing tests must stay green:** 54/54 in `devmgr_tests` after every task. Run the full suite, not just the new tests.
- **Gates per task:** build clean via `cmake --build --preset linux-debug`; `ctest --test-dir build/linux-debug --output-on-failure` all pass; `clang-format --dry-run --Werror` clean on every touched file.
- **Include style:** repo-root-relative for non-installed headers (`#include "gui/src/qt_ui_dispatcher.hpp"`), matching `tui/`.
- **Naming:** namespace `devmgr::gui`; static lib `devmgr_gui`; executable `devmgr-gui`; test binary `devmgr_gui_tests`.
- Configure/build/test commands (from repo root):
  - `cmake --preset linux-debug` (re-run after CMake edits; if Qt was installed after a prior configure, the cached `DEVMGR_BUILD_GUI=OFF` persists — pass `-DDEVMGR_BUILD_GUI=ON` once or delete `build/linux-debug/CMakeCache.txt`)
  - `cmake --build --preset linux-debug`
  - `ctest --test-dir build/linux-debug --output-on-failure`

## File Structure (end state)

```
gui/
  CMakeLists.txt              # devmgr_gui lib + devmgr-gui exe + devmgr_gui_tests + selftest ctest
  src/
    qt_ui_dispatcher.hpp/.cpp # IUiDispatcher over Qt; taskExecuted() signal (Task 2)
    device_list_model.hpp/.cpp# flat QAbstractListModel over DeviceListVM (Task 3)
    main_window.hpp/.cpp      # parity window: filter/list/detail/status/refresh (Task 4)
    gui_app.hpp/.cpp          # runGuiApp() composition root + --self-test (Task 5)
    main.cpp                  # thin main → runGuiApp (Task 5)
  tests/
    gui_test_main.cpp         # GTest main constructing offscreen QApplication (Task 2)
    test_qt_ui_dispatcher.cpp # Task 2
    test_device_list_model.cpp# Task 3
    test_main_window.cpp      # Task 4
app/include/devmgr/app/device_list_vm.hpp   # + isHeader(), setRebuildHooks() (Task 1)
app/src/device_list_vm.cpp                  # hook invocation in rebuild() (Task 1)
tests/unit/test_device_list_vm.cpp          # + 2 tests (Task 1)
CMakeLists.txt                              # Qt6 detection + DEVMGR_BUILD_GUI + add_subdirectory(gui) (Task 2)
Dockerfile                                  # + qt6-base-dev (Task 6)
.github/workflows/ci.yml                    # format/tidy cover gui/; purity guard (Task 6)
```

---

### Task 1: `DeviceListVM` additions — `isHeader()` + rebuild hooks

The only `devmgr_app` change of the phase. Toolkit-agnostic; the TUI is untouched.

**Files:**
- Modify: `app/include/devmgr/app/device_list_vm.hpp`
- Modify: `app/src/device_list_vm.cpp`
- Test: `tests/unit/test_device_list_vm.cpp`

**Interfaces:**
- Consumes: existing `DeviceListVM` internals (`rowIds_`, `rebuild()`).
- Produces (Task 3 depends on these exact signatures):
  - `bool DeviceListVM::isHeader(int row) const` — true iff `row` is in range and is a group-header/placeholder row (`rowIds_[row] == nullopt`); false out of range.
  - `void DeviceListVM::setRebuildHooks(std::function<void()> before, std::function<void()> after)` — hooks invoked at entry/exit of every `rebuild()` (delta-triggered and filter-triggered alike). Default-constructed hooks are no-ops. Must be set/cleared on the UI thread only (same thread `rebuild()` runs on).

- [x] **Step 1: Write the failing tests**

Append to `tests/unit/test_device_list_vm.cpp` (uses the existing `Fixture` and `dev()` helper at the top of the file):

```cpp
TEST(DeviceListVmTest, IsHeaderDistinguishesHeaderAndDeviceRows) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    f.facade.refresh().wait();

    ASSERT_EQ(vm.rowsRef().size(), 2u);  // one USB header + one device
    int headers = 0;
    int devices = 0;
    for (int i = 0; std::cmp_less(i, vm.rowsRef().size()); ++i) {
        vm.selectedRef() = i;
        if (vm.isHeader(i)) {
            ++headers;
            EXPECT_FALSE(vm.selectedDeviceId().has_value());  // header ⇔ no DeviceId
        } else {
            ++devices;
            EXPECT_TRUE(vm.selectedDeviceId().has_value());
        }
    }
    EXPECT_EQ(headers, 1);
    EXPECT_EQ(devices, 1);
    EXPECT_FALSE(vm.isHeader(-1));    // out of range is never a header
    EXPECT_FALSE(vm.isHeader(9999));
}

TEST(DeviceListVmTest, RebuildHooksBracketDeltaAndFilterRebuilds) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    app::DeviceListVM vm(f.facade, f.bus, f.dispatcher);
    std::vector<std::string> log;
    vm.setRebuildHooks([&] { log.emplace_back("before"); }, [&] { log.emplace_back("after"); });

    f.facade.refresh().wait();  // one Added event → one rebuild (InlineUiDispatcher: synchronous)
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "before");
    EXPECT_EQ(log[1], "after");

    vm.setFilter("mouse");  // filter path calls rebuild() directly — must also be bracketed
    ASSERT_EQ(log.size(), 4u);
    EXPECT_EQ(log[2], "before");
    EXPECT_EQ(log[3], "after");

    vm.setRebuildHooks({}, {});  // cleared hooks: rebuild must not crash and log stays frozen
    vm.setFilter("");
    EXPECT_EQ(log.size(), 4u);
}
```

The file already includes `<utility>` transitively via gtest? It does not — `std::cmp_less` is already used at line 107 of the existing file (compiles today), so no new include is needed.

- [x] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R DeviceListVmTest --output-on-failure`
Expected: BUILD FAILURE — `no member named 'isHeader' in 'devmgr::app::DeviceListVM'` (a compile failure is this step's "failing test").

- [x] **Step 3: Implement**

In `app/include/devmgr/app/device_list_vm.hpp`, add `#include <functional>` to the includes (it is not there today), then add to the public section after `selectedDeviceId()`:

```cpp
    // True iff `row` is a group-header or placeholder row (no DeviceId behind
    // it); false for device rows and for out-of-range rows. Lets a frontend
    // render/disable headers without re-deriving the grouping.
    bool isHeader(int row) const;
    // Frontend hooks invoked at entry/exit of every rebuild() — the single
    // funnel for all row mutation (delta-triggered posts and setFilter alike).
    // Qt uses them for beginResetModel()/endResetModel(); the TUI leaves them
    // unset. Default-constructed hooks are no-ops. UI-thread only: set/clear
    // them on the same thread rebuild() runs on.
    void setRebuildHooks(std::function<void()> before, std::function<void()> after) {
        beforeRebuild_ = std::move(before);
        afterRebuild_ = std::move(after);
    }
```

And to the private members (next to `selected_`):

```cpp
    std::function<void()> beforeRebuild_;
    std::function<void()> afterRebuild_;
```

In `app/src/device_list_vm.cpp`:

1. Wrap `rebuild()`'s body — first statement and last statement of the function:

```cpp
void DeviceListVM::rebuild() {
    if (beforeRebuild_) beforeRebuild_();
    // ... entire existing body unchanged ...
    if (afterRebuild_) afterRebuild_();
}
```

(`rebuild()` has no early returns today; if one is ever added, the exit hook must move with it — note this in a comment only if you add such a return, which this task must not.)

2. Add `isHeader` next to `selectedDeviceId()` (same bounds-check idiom):

```cpp
bool DeviceListVM::isHeader(int row) const {
    if (row < 0 || std::cmp_greater_equal(row, rowIds_.size())) return false;
    return !rowIds_[static_cast<std::size_t>(row)].has_value();
}
```

- [x] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug --output-on-failure`
Expected: all tests pass — 54 existing + 2 new = 56.

- [x] **Step 5: Format gate**

Run: `clang-format --dry-run --Werror app/include/devmgr/app/device_list_vm.hpp app/src/device_list_vm.cpp tests/unit/test_device_list_vm.cpp`
Expected: no output, exit 0. If it fails, run the same command with `-i` instead of `--dry-run --Werror` and re-verify.

- [x] **Step 6: USER commits (agent must not)** — committed `86238c2`

Suggested message:

```
feat(app): DeviceListVM isHeader() + rebuild hooks — toolkit-agnostic seam for Qt model resets
```

---

### Task 2: GUI scaffold — CMake gating, `QtUiDispatcher`, offscreen test harness

**Files:**
- Modify: `CMakeLists.txt` (root)
- Create: `gui/CMakeLists.txt`
- Create: `gui/src/qt_ui_dispatcher.hpp`
- Create: `gui/src/qt_ui_dispatcher.cpp`
- Create: `gui/tests/gui_test_main.cpp`
- Test: `gui/tests/test_qt_ui_dispatcher.cpp`

**Interfaces:**
- Consumes: `app::IUiDispatcher` (`app/include/devmgr/app/ui_dispatcher.hpp`): `void post(std::function<void()> fn)`.
- Produces (Tasks 3–5 depend on these):
  - `class devmgr::gui::QtUiDispatcher final : public QObject, public app::IUiDispatcher` — default-constructible on the GUI thread; `void post(std::function<void()>) override`; signal `void taskExecuted()` emitted on the GUI thread after each posted closure runs.
  - CMake target `devmgr_gui` (static lib, links `devmgr_app devmgr_core devmgr_pal_linux Qt6::Widgets` PUBLIC) and test binary `devmgr_gui_tests`.
  - Root option `DEVMGR_BUILD_GUI` (default: ON iff Qt6 found on Linux).

- [x] **Step 1: CMake gating (root)**

In root `CMakeLists.txt`, replace the `if(UNIX AND NOT APPLE)` block with:

```cmake
if(UNIX AND NOT APPLE)
    add_subdirectory(platform/linux)
    add_subdirectory(tui)
    # Qt 6 GUI: auto-ON where system Qt is present, so the tree still
    # configures and builds with no Qt installed. (System packages by design —
    # see the Phase 3 spec; vcpkg Qt is deliberately not used.)
    find_package(Qt6 COMPONENTS Widgets Test QUIET)
    option(DEVMGR_BUILD_GUI "Build the Qt 6 GUI (devmgr-gui + its tests)" ${Qt6_FOUND})
    if(DEVMGR_BUILD_GUI)
        add_subdirectory(gui)
    endif()
endif()
```

- [x] **Step 2: `gui/CMakeLists.txt`**

```cmake
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

set(CMAKE_AUTOMOC ON)

add_library(devmgr_gui STATIC
    src/qt_ui_dispatcher.cpp)
target_include_directories(devmgr_gui PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(devmgr_gui
    PUBLIC devmgr_app devmgr_core devmgr_pal_linux Qt6::Widgets)
target_compile_features(devmgr_gui PUBLIC cxx_std_20)

add_executable(devmgr_gui_tests
    tests/gui_test_main.cpp
    tests/test_qt_ui_dispatcher.cpp)
target_include_directories(devmgr_gui_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(devmgr_gui_tests PRIVATE devmgr_gui Qt6::Test GTest::gtest)
gtest_discover_tests(devmgr_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

(No `devmgr-gui` executable yet — that is Task 5; a lib + tests is a complete, reviewable deliverable.)

- [x] **Step 3: Write the failing test**

`gui/tests/gui_test_main.cpp` — the shared GTest main for all GUI tests:

```cpp
#include <gtest/gtest.h>

#include <QApplication>

// Offscreen everywhere (CI containers and interactive host runs alike): these
// tests exercise models/widgets, not a compositor. Must be set before the
// QApplication is constructed.
int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

`gui/tests/test_qt_ui_dispatcher.cpp`:

```cpp
#include <atomic>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QThread>

#include "gui/src/qt_ui_dispatcher.hpp"

using devmgr::gui::QtUiDispatcher;

namespace {
// Deliver queued cross-thread posts to this (the GUI) thread.
void pumpUntil(const std::atomic<bool>& done) {
    for (int i = 0; i < 100 && !done.load(); ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}
}  // namespace

TEST(QtUiDispatcherTest, PostFromWorkerThreadRunsOnGuiThread) {
    QtUiDispatcher dispatcher;
    std::atomic<bool> ran{false};
    QThread* runThread = nullptr;
    std::thread worker([&] {
        dispatcher.post([&] {
            runThread = QThread::currentThread();
            ran.store(true);
        });
    });
    worker.join();
    pumpUntil(ran);
    ASSERT_TRUE(ran.load());
    EXPECT_EQ(runThread, QCoreApplication::instance()->thread());
}

TEST(QtUiDispatcherTest, PostOnGuiThreadRunsImmediately) {
    QtUiDispatcher dispatcher;
    bool ran = false;
    dispatcher.post([&] { ran = true; });
    // Auto-connection on the owning thread == direct call — this synchronicity
    // is what lets same-thread test drivers (Tasks 3–4) skip event pumping.
    EXPECT_TRUE(ran);
}

TEST(QtUiDispatcherTest, TaskExecutedFiresAfterEachPost) {
    QtUiDispatcher dispatcher;
    QSignalSpy spy(&dispatcher, &QtUiDispatcher::taskExecuted);
    dispatcher.post([] {});
    dispatcher.post([] {});
    EXPECT_EQ(spy.count(), 2);
}

TEST(QtUiDispatcherTest, PostsQueuedAtDestructionAreDropped) {
    auto dispatcher = std::make_unique<QtUiDispatcher>();
    std::atomic<bool> ran{false};
    std::thread worker([&] { dispatcher->post([&] { ran.store(true); }); });
    worker.join();       // the closure is now queued for the GUI thread
    dispatcher.reset();  // destroy the context before delivery
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    EXPECT_FALSE(ran.load());  // dropped, never delivered — documented contract
}
```

- [x] **Step 4: Run to verify it fails**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `gui/src/qt_ui_dispatcher.hpp: No such file or directory`.
(Actual failure surfaced one step earlier: CMake generate fails on the missing `src/qt_ui_dispatcher.cpp` referenced by `add_library` — same failing state, confirmed.)

- [x] **Step 5: Implement `QtUiDispatcher`**

`gui/src/qt_ui_dispatcher.hpp`:

```cpp
#pragma once
#include <functional>

#include <QObject>

#include "devmgr/app/ui_dispatcher.hpp"

namespace devmgr::gui {

// IUiDispatcher over Qt: post() marshals the closure onto the thread this
// object was created on (the GUI thread) via the auto-connection functor
// overload of QMetaObject::invokeMethod — queued from other threads, direct
// when already on the GUI thread. taskExecuted() fires on the GUI thread
// after each closure runs; MainWindow uses it the way the TUI uses
// Event::Custom (re-read cheap derived state such as StatusLineVM::text()).
//
// Qt-floor note: do NOT switch to the functor + Qt::ConnectionType overload
// of invokeMethod — it requires Qt >= 6.7 and the CI floor is Ubuntu 24.04's
// Qt 6.4 (see the Phase 3 spec's "Minimum Qt" decision).
//
// Teardown contract: once this QObject is destroyed, queued-but-undelivered
// posts are dropped silently (never run). The composition root must stop all
// producers (hotplug monitor, DelayedScheduler, in-flight refreshes) before
// this object and the VMs posting into it unwind — devmgr-gui mirrors
// tui/src/tui_app.cpp's teardown ordering, which exists for the same reason.
class QtUiDispatcher final : public QObject, public app::IUiDispatcher {
    Q_OBJECT
   public:
    void post(std::function<void()> fn) override;

   signals:
    void taskExecuted();  // on the GUI thread, after a posted closure ran
};

}  // namespace devmgr::gui
```

`gui/src/qt_ui_dispatcher.cpp`:

```cpp
#include "gui/src/qt_ui_dispatcher.hpp"

#include <utility>

#include <QMetaObject>

namespace devmgr::gui {

void QtUiDispatcher::post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [this, fn = std::move(fn)] {
        fn();
        emit taskExecuted();
    });
}

}  // namespace devmgr::gui
```

- [x] **Step 6: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R QtUiDispatcherTest --output-on-failure`
Expected: 4/4 QtUiDispatcherTest pass. Then run the FULL suite: `ctest --test-dir build/linux-debug --output-on-failure` — 60 tests total (56 + 4), all pass.

- [x] **Step 7: Verify the no-Qt configure path still works**

Run: `cmake -B /tmp/devmgr-noqt-check -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake -DDEVMGR_BUILD_GUI=OFF && rm -rf /tmp/devmgr-noqt-check`
Expected: configures successfully with no Qt targets (proves the guarded-build requirement without uninstalling Qt).

- [x] **Step 8: Format gate**

Run: `clang-format --dry-run --Werror gui/src/*.hpp gui/src/*.cpp gui/tests/*.cpp`
Expected: clean. Fix with `-i` if not.

- [x] **Step 9: USER commits** — committed `345fef6`

```
feat(gui): scaffold Qt 6 GUI — CMake gating + QtUiDispatcher + offscreen GTest harness
```

---

### Task 3: `DeviceListModel` — flat Qt model over the shared VM

**Files:**
- Create: `gui/src/device_list_model.hpp`
- Create: `gui/src/device_list_model.cpp`
- Modify: `gui/CMakeLists.txt` (add source + test file)
- Test: `gui/tests/test_device_list_model.cpp`

**Interfaces:**
- Consumes: Task 1's `vm.isHeader(int)`, `vm.setRebuildHooks(before, after)`, plus existing `vm.rowsRef()`; Task 2's `QtUiDispatcher`.
- Produces (Task 4 depends on this): `class devmgr::gui::DeviceListModel final : public QAbstractListModel` — `explicit DeviceListModel(app::DeviceListVM& vm, QObject* parent = nullptr)`; standard `rowCount`/`data`/`flags` overrides; emits `modelAboutToBeReset`/`modelReset` around every VM rebuild; destructor unregisters the hooks.

- [x] **Step 1: Write the failing tests**

`gui/tests/test_device_list_model.cpp`:

```cpp
#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

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

// Same shape as tests/unit/test_device_list_vm.cpp's Fixture, but with the
// real QtUiDispatcher: cross-thread posts (refresh on the scheduler) are
// delivered by processEvents() on this thread; same-thread publishes
// (applyDelta below) run the rebuild synchronously via auto-connection.
struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc};
    app::DeviceListVM vm{facade, bus, dispatcher};

    void refreshAndPump() {
        facade.refresh().wait();               // publish happened → rebuild queued
        QCoreApplication::processEvents();     // deliver it on this (GUI) thread
    }
};
}  // namespace

TEST(DeviceListModelTest, InvariantsHoldAcrossRefreshFilterAndDelta) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "GPU"));
    gui::DeviceListModel model(f.vm);
    // Fatal => any QAbstractItemModel contract violation aborts the test run.
    QAbstractItemModelTester tester(&model,
                                    QAbstractItemModelTester::FailureReportingMode::Fatal);

    f.refreshAndPump();
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));

    f.vm.setFilter("mouse");  // direct rebuild on this thread, reset-bracketed
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));

    f.svc.applyDelta(pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added,
                                       .device = dev("u2", core::BusType::Usb, "Keyboard")});
    f.vm.setFilter("");  // clear the filter so both devices are visible again
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));
}

TEST(DeviceListModelTest, ResetSignalsBracketEveryRebuild) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    gui::DeviceListModel model(f.vm);
    QSignalSpy aboutSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    f.refreshAndPump();
    EXPECT_EQ(aboutSpy.count(), 1);
    EXPECT_EQ(resetSpy.count(), 1);

    f.vm.setFilter("nomatch");
    EXPECT_EQ(aboutSpy.count(), 2);
    EXPECT_EQ(resetSpy.count(), 2);
}

TEST(DeviceListModelTest, HeaderRowsAreDisabledDeviceRowsSelectable) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    gui::DeviceListModel model(f.vm);
    f.refreshAndPump();

    ASSERT_EQ(model.rowCount(), 2);  // header + device
    ASSERT_TRUE(f.vm.isHeader(0));
    EXPECT_EQ(model.flags(model.index(0, 0)), Qt::NoItemFlags);
    EXPECT_TRUE(model.flags(model.index(1, 0)).testFlag(Qt::ItemIsSelectable));
    EXPECT_TRUE(model.flags(model.index(1, 0)).testFlag(Qt::ItemIsEnabled));
}

TEST(DeviceListModelTest, DataMirrorsVmRows) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "GPU"));
    gui::DeviceListModel model(f.vm);
    f.refreshAndPump();

    for (int i = 0; i < model.rowCount(); ++i) {
        EXPECT_EQ(model.data(model.index(i, 0), Qt::DisplayRole).toString().toStdString(),
                  f.vm.rowsRef()[static_cast<std::size_t>(i)]);
    }
    EXPECT_FALSE(model.data(model.index(0, 0), Qt::DecorationRole).isValid());  // only DisplayRole
}
```

Add to `gui/CMakeLists.txt`: `src/device_list_model.cpp` under `devmgr_gui` sources and `tests/test_device_list_model.cpp` under `devmgr_gui_tests` sources.

- [x] **Step 2: Run to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `gui/src/device_list_model.hpp: No such file or directory`.
(Actual: CMake generate error on missing `src/device_list_model.cpp` — same failing state as Task 2 Step 4.)

- [x] **Step 3: Implement**

`gui/src/device_list_model.hpp`:

```cpp
#pragma once
#include <QAbstractListModel>

#include "devmgr/app/device_list_vm.hpp"

namespace devmgr::gui {

// Flat Qt adapter over the shared DeviceListVM (Phase 3 spec, Approach A).
// The VM's rows stay the single source of truth — this model stores nothing.
// Every VM rebuild (hotplug delta or filter keystroke) is bracketed with
// beginResetModel()/endResetModel() via the VM's rebuild hooks, so views
// re-query exactly once per rebuild. Group-header rows are disabled and
// non-selectable; MainWindow re-applies the VM's DeviceId-preserved selection
// after each reset.
//
// Lifetime: the VM must outlive this model (the composition root and tests
// guarantee it by declaration order); the destructor unregisters the hooks so
// a later VM rebuild never calls into a destroyed model.
class DeviceListModel final : public QAbstractListModel {
    Q_OBJECT
   public:
    explicit DeviceListModel(app::DeviceListVM& vm, QObject* parent = nullptr);
    ~DeviceListModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

   private:
    app::DeviceListVM& vm_;
};

}  // namespace devmgr::gui
```

`gui/src/device_list_model.cpp`:

```cpp
#include "gui/src/device_list_model.hpp"

#include <cstddef>

namespace devmgr::gui {

DeviceListModel::DeviceListModel(app::DeviceListVM& vm, QObject* parent)
    : QAbstractListModel(parent), vm_(vm) {
    vm_.setRebuildHooks([this] { beginResetModel(); }, [this] { endResetModel(); });
}

DeviceListModel::~DeviceListModel() { vm_.setRebuildHooks({}, {}); }

int DeviceListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;  // flat list
    return static_cast<int>(vm_.rowsRef().size());
}

QVariant DeviceListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 ||
        index.row() >= static_cast<int>(vm_.rowsRef().size()))
        return {};
    if (role != Qt::DisplayRole) return {};
    return QString::fromStdString(vm_.rowsRef()[static_cast<std::size_t>(index.row())]);
}

Qt::ItemFlags DeviceListModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (vm_.isHeader(index.row())) return Qt::NoItemFlags;  // headers: unselectable
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

}  // namespace devmgr::gui
```

- [x] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R DeviceListModelTest --output-on-failure`
Expected: 4/4 pass (the ModelTester runs its checks inside the first test — a contract violation aborts loudly). Then full suite: 64 tests, all pass.

- [x] **Step 5: Format gate**

Run: `clang-format --dry-run --Werror gui/src/device_list_model.hpp gui/src/device_list_model.cpp gui/tests/test_device_list_model.cpp`
Expected: clean.

- [x] **Step 6: USER commits** — committed `faa1a0d`

```
feat(gui): DeviceListModel — flat QAbstractListModel over shared DeviceListVM, reset-bracketed rebuilds
```

---

### Task 4: `MainWindow` — parity UI with selection sync, detail pane, status bar

**Files:**
- Create: `gui/src/main_window.hpp`
- Create: `gui/src/main_window.cpp`
- Modify: `gui/CMakeLists.txt` (add source + test file)
- Test: `gui/tests/test_main_window.cpp`

**Interfaces:**
- Consumes: Task 3's `DeviceListModel`; Task 2's `QtUiDispatcher::taskExecuted()`; existing `app::DeviceListVM` (`setFilter`, `selectedRef`, `selectedDeviceId`, `isHeader`), `app::DeviceDetailVM::lines(std::optional<core::DeviceId>)`, `app::StatusLineVM::text()`.
- Produces (Task 5 depends on this): `class devmgr::gui::MainWindow final : public QMainWindow` with constructor `MainWindow(app::DeviceListVM&, app::DeviceDetailVM&, app::StatusLineVM&, QtUiDispatcher&, std::function<void()> onRefresh, QWidget* parent = nullptr)`. Test accessors: `QListView* listView() const`, `QTreeWidget* detailTree() const`, `QLineEdit* filterEdit() const`.

Behavior contract (mirrors the TUI):
- Filter box text change → `listVm.setFilter(text)` (which rebuilds synchronously → model reset).
- Toolbar **Refresh** action → invokes `onRefresh` (the composition root owns the future-draining lifetime, exactly like `tui_app.cpp`'s `pending` vector).
- View `currentChanged` → writes `listVm.selectedRef()` (device rows only; headers are unselectable via flags) → rebuilds the detail pane.
- `modelReset` → re-apply `listVm.selectedRef()` as the view's current index (the VM already re-resolved it by `DeviceId` during rebuild) → rebuild the detail pane (properties may have changed under the same selection).
- `dispatcher.taskExecuted` → re-read `statusVm.text()` into the status bar (the Qt analogue of the TUI re-rendering on `Event::Custom`; `StatusLineVM` posts a wake closure on every message set/clear).
- Detail pane: two-column read-only `QTreeWidget` (Field | Value). `DeviceDetailVM::lines()` emits `"Label:   value"` strings — split each on the **first** `':'`, trim both halves; a line with no `':'` (e.g. `"(no device selected)"`) goes into column 0 alone. No per-frame caching (deliberate deviation from the TUI's detail cache: Qt only calls these slots on sparse events, not per frame).

- [x] **Step 1: Write the failing tests**

`gui/tests/test_main_window.cpp`:

```cpp
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QLineEdit>
#include <QListView>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "gui/src/main_window.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

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
    runtime::DelayedScheduler delayed;
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc};
    app::DeviceListVM listVm{facade, bus, dispatcher};
    app::DeviceDetailVM detailVm{facade};
    app::StatusLineVM statusVm{bus, delayed, dispatcher};
    int refreshCalls = 0;

    gui::MainWindow makeWindow() {
        return gui::MainWindow(listVm, detailVm, statusVm, dispatcher,
                               [this] { ++refreshCalls; });
    }
    void refreshAndPump() {
        facade.refresh().wait();
        QCoreApplication::processEvents();
    }
    // First selectable (non-header) row, or -1.
    int firstDeviceRow() const {
        for (int i = 0; std::cmp_less(i, listVm.rowsRef().size()); ++i)
            if (!listVm.isHeader(i)) return i;
        return -1;
    }
};
}  // namespace

TEST(MainWindowTest, SelectionFillsDetailPaneWithKeyValueRows) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();

    const int row = f.firstDeviceRow();
    ASSERT_GE(row, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(row, 0));

    auto* tree = window.detailTree();
    ASSERT_GE(tree->topLevelItemCount(), 2);
    // First detail line is "Name:    <name>" → split into ("Name", "Mouse").
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("Name"));
    EXPECT_EQ(tree->topLevelItem(0)->text(1), QStringLiteral("Mouse"));
}

TEST(MainWindowTest, SelectionSurvivesModelResetByDeviceId) {
    Fixture f;
    f.pal.seedDevice(dev("dev-beta", core::BusType::Usb, "Beta"));
    auto window = f.makeWindow();
    f.refreshAndPump();

    const int betaRow = f.firstDeviceRow();
    ASSERT_GE(betaRow, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(betaRow, 0));

    // "Alpha" sorts before "Beta" inside the USB group → Beta's row index shifts.
    f.svc.applyDelta(pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added,
                                       .device = dev("dev-alpha", core::BusType::Usb, "Alpha")});

    // The VM re-resolved selection by DeviceId; the view must follow it.
    ASSERT_TRUE(f.listVm.selectedDeviceId().has_value());
    EXPECT_EQ(f.listVm.selectedDeviceId()->value, "dev-beta");
    EXPECT_EQ(window.listView()->currentIndex().row(), f.listVm.selectedRef());
    EXPECT_EQ(window.detailTree()->topLevelItem(0)->text(1), QStringLiteral("Beta"));
}

TEST(MainWindowTest, FilterEditDrivesVmAndModel) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Logitech Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "NVIDIA GPU"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    const int allRows = static_cast<int>(f.listVm.rowsRef().size());

    window.filterEdit()->setText(QStringLiteral("mouse"));  // fires textChanged

    EXPECT_LT(static_cast<int>(f.listVm.rowsRef().size()), allRows);
    EXPECT_EQ(window.listView()->model()->rowCount(),
              static_cast<int>(f.listVm.rowsRef().size()));
}

TEST(MainWindowTest, RefreshActionInvokesInjectedCallback) {
    Fixture f;
    auto window = f.makeWindow();
    auto actions = window.findChildren<QToolBar*>().first()->actions();
    ASSERT_FALSE(actions.isEmpty());
    actions.first()->trigger();
    EXPECT_EQ(f.refreshCalls, 1);
}

TEST(MainWindowTest, StatusBarShowsTransientHotplugMessage) {
    Fixture f;
    auto window = f.makeWindow();
    f.statusVm.arm();  // as the composition root does after the initial refresh

    f.svc.applyDelta(pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added,
                                       .device = dev("u9", core::BusType::Usb, "Webcam")});

    // Same-thread publish → StatusLineVM::setMessage → dispatcher wake runs
    // directly → taskExecuted → status bar updated, no pumping needed.
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("added")));
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("Webcam")));
}
```

Add to `gui/CMakeLists.txt`: `src/main_window.cpp` under `devmgr_gui`, `tests/test_main_window.cpp` under `devmgr_gui_tests`.

Note: `<utility>` for `std::cmp_less` comes in via the app headers already included; add `#include <utility>` explicitly at the top of the test file anyway (include-what-you-use).

- [x] **Step 2: Run to verify it fails**

Run: `cmake --build --preset linux-debug`
Expected: BUILD FAILURE — `gui/src/main_window.hpp: No such file or directory`.
(Actual: CMake generate error on missing `src/main_window.cpp` — same failing state as Tasks 2–3.)

- [x] **Step 3: Implement**

`gui/src/main_window.hpp`:

```cpp
#pragma once
#include <functional>

#include <QMainWindow>

#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

class QLineEdit;
class QListView;
class QTreeWidget;

namespace devmgr::gui {

// Strict-parity main window (Phase 3 spec): filter box + grouped device list
// on the left, two-column key/value detail pane on the right, toolbar Refresh,
// transient status bar. The shared ViewModels remain the source of truth;
// widgets mirror them. onRefresh is injected so the composition root owns the
// pending-refresh-futures lifetime (the drain-before-teardown contract in
// tui/src/tui_app.cpp applies identically to the GUI root).
class MainWindow final : public QMainWindow {
    Q_OBJECT
   public:
    MainWindow(app::DeviceListVM& listVm, app::DeviceDetailVM& detailVm,
               app::StatusLineVM& statusVm, QtUiDispatcher& dispatcher,
               std::function<void()> onRefresh, QWidget* parent = nullptr);

    // Test accessors (offscreen tests drive/inspect the real widgets).
    QListView* listView() const { return listView_; }
    QTreeWidget* detailTree() const { return detailTree_; }
    QLineEdit* filterEdit() const { return filterEdit_; }

   private:
    void syncSelectionFromVm();  // after modelReset: VM re-resolved by DeviceId
    void updateDetailPane();
    void updateStatusBar();

    app::DeviceListVM& listVm_;
    app::DeviceDetailVM& detailVm_;
    app::StatusLineVM& statusVm_;
    std::function<void()> onRefresh_;
    DeviceListModel* model_ = nullptr;  // Qt-parented to this window
    QListView* listView_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QTreeWidget* detailTree_ = nullptr;
};

}  // namespace devmgr::gui
```

`gui/src/main_window.cpp`:

```cpp
#include "gui/src/main_window.hpp"

#include <cstddef>
#include <string>
#include <utility>

#include <QAction>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace devmgr::gui {

MainWindow::MainWindow(app::DeviceListVM& listVm, app::DeviceDetailVM& detailVm,
                       app::StatusLineVM& statusVm, QtUiDispatcher& dispatcher,
                       std::function<void()> onRefresh, QWidget* parent)
    : QMainWindow(parent),
      listVm_(listVm),
      detailVm_(detailVm),
      statusVm_(statusVm),
      onRefresh_(std::move(onRefresh)) {
    setWindowTitle(QStringLiteral("Device Manager"));

    auto* toolbar = addToolBar(QStringLiteral("main"));
    toolbar->setMovable(false);
    auto* refreshAction = toolbar->addAction(QStringLiteral("Refresh"));
    connect(refreshAction, &QAction::triggered, this, [this] { onRefresh_(); });

    filterEdit_ = new QLineEdit;
    filterEdit_->setPlaceholderText(QStringLiteral("filter devices…"));
    connect(filterEdit_, &QLineEdit::textChanged, this,
            [this](const QString& text) { listVm_.setFilter(text.toStdString()); });

    model_ = new DeviceListModel(listVm_, this);
    listView_ = new QListView;
    listView_->setModel(model_);
    listView_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    detailTree_ = new QTreeWidget;
    detailTree_->setColumnCount(2);
    detailTree_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Value")});
    detailTree_->setRootIsDecorated(false);
    detailTree_->setSelectionMode(QAbstractItemView::NoSelection);

    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(filterEdit_);
    leftLayout->addWidget(listView_);

    auto* splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(detailTree_);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    // View selection mirrors the VM (headers can't be selected — model flags).
    connect(listView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex& current, const QModelIndex&) {
                if (current.isValid()) listVm_.selectedRef() = current.row();
                updateDetailPane();
            });
    // After a rebuild the VM has re-resolved the selection by DeviceId; the
    // reset cleared the view's currentIndex, so re-apply the VM's row and
    // rebuild the detail pane (properties may have changed under the same id).
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        syncSelectionFromVm();
        updateDetailPane();
    });
    // The Qt analogue of the TUI re-rendering on Event::Custom: StatusLineVM
    // posts a wake closure on every message set/clear; re-read text() then.
    connect(&dispatcher, &QtUiDispatcher::taskExecuted, this,
            [this] { updateStatusBar(); });

    updateDetailPane();  // "(no device selected)" until something is chosen
}

void MainWindow::syncSelectionFromVm() {
    const int row = listVm_.selectedRef();
    if (row >= 0 && row < model_->rowCount() && !listVm_.isHeader(row))
        listView_->setCurrentIndex(model_->index(row, 0));
}

void MainWindow::updateDetailPane() {
    detailTree_->clear();
    for (const std::string& line : detailVm_.lines(listVm_.selectedDeviceId())) {
        auto* item = new QTreeWidgetItem(detailTree_);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            item->setText(0, QString::fromStdString(line));
        } else {
            item->setText(0, QString::fromStdString(line.substr(0, colon)).trimmed());
            item->setText(1, QString::fromStdString(line.substr(colon + 1)).trimmed());
        }
    }
    detailTree_->resizeColumnToContents(0);
}

void MainWindow::updateStatusBar() {
    statusBar()->showMessage(QString::fromStdString(statusVm_.text()));
}

}  // namespace devmgr::gui
```

- [x] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R MainWindowTest --output-on-failure`
Expected: 5/5 pass. Then full suite: 69 tests, all pass. (Confirmed: 5/5, then 69/69.)

- [x] **Step 5: Format gate**

Run: `clang-format --dry-run --Werror gui/src/main_window.hpp gui/src/main_window.cpp gui/tests/test_main_window.cpp`
Expected: clean. (Plan-verbatim wraps needed `-i` touch-up — 3 lines rewrapped; suite re-verified 69/69 after.)

- [ ] **Step 6: USER commits**

```
feat(gui): MainWindow — parity list/detail/filter/status UI with VM-mirrored selection
```

---

### Task 5: Composition root `runGuiApp()` + `--self-test` offscreen smoke

**Files:**
- Create: `gui/src/gui_app.hpp`
- Create: `gui/src/gui_app.cpp`
- Create: `gui/src/main.cpp`
- Modify: `gui/CMakeLists.txt` (add `gui_app.cpp` to lib; add `devmgr-gui` executable; register the selftest ctest)

**Interfaces:**
- Consumes: everything above, plus (all existing, used identically by `tui/src/tui_app.cpp`): `platform_linux::UdevDeviceEnumerator`, `platform_linux::UdevHotplugMonitor`, `app::DeviceService(bus)`, `app::ApplicationFacade(enumerator, scheduler, bus, service)`, `app::HotplugService(monitor, service, delayed)`, `hotplug.start()/stop()`, `delayed.shutdown()`, `core::ErrorEvent{.source, .message}`, `facade.refresh() -> std::future<void>`.
- Produces: `int devmgr::gui::runGuiApp(int argc, char** argv)`; `devmgr-gui` binary; ctest `devmgr_gui_selftest`.

**Teardown ordering is load-bearing** — it is a line-for-line mirror of `tui/src/tui_app.cpp` (read its comments before touching this): declaration order guarantees hotplug/delayed outlive the VMs/dispatcher; stop sources → drain refresh futures → unwind; the catch-block duplicates the stop/drain so exception unwind is equally safe.

- [ ] **Step 1: Implement `gui_app` (no new unit test — this task's test cycle is the selftest smoke below plus the full existing suite)**

`gui/src/gui_app.hpp`:

```cpp
#pragma once

namespace devmgr::gui {

// Composition root for devmgr-gui. Mirrors tui/src/tui_app.cpp::runTuiApp()
// construction and teardown ordering exactly — see the comments there; every
// one of them (widened try, stop-then-drain, declaration order) applies here.
// With "--self-test" in argv: construct the full wiring, run one enumeration
// and rebuild, print the row count to stdout, and exit 0 without showing a
// window (CI runs this under QT_QPA_PLATFORM=offscreen).
int runGuiApp(int argc, char** argv);

}  // namespace devmgr::gui
```

`gui/src/gui_app.cpp`:

```cpp
#include "gui/src/gui_app.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <vector>

#include <QApplication>
#include <QCoreApplication>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/hotplug_service.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/platform/linux/udev_device_enumerator.hpp"
#include "devmgr/platform/linux/udev_hotplug_monitor.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "gui/src/main_window.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

namespace devmgr::gui {
namespace {
// Same helper as tui/src/tui_app.cpp's drainPending (deliberately duplicated:
// 5 lines; hoisting it into devmgr_app would be interface churn for nothing).
void drainPending(std::vector<std::future<void>>& pending) {
    for (auto& f : pending) {
        if (f.valid()) f.wait();
    }
}
}  // namespace

int runGuiApp(int argc, char** argv) {
    QApplication qapp(argc, argv);
    const bool selfTest = QCoreApplication::arguments().contains(QStringLiteral("--self-test"));

    // Declaration order below is the teardown contract (reverse-destruction):
    // hotplug/delayed MUST be declared before the dispatcher and VMs so they
    // are still alive when the VMs unwind — identical to runTuiApp().
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;
    runtime::DelayedScheduler delayed;
    platform_linux::UdevDeviceEnumerator enumerator;
    platform_linux::UdevHotplugMonitor monitor;
    app::DeviceService service(bus);
    app::ApplicationFacade facade(enumerator, scheduler, bus, service);
    app::HotplugService hotplug(monitor, service, delayed);  // 250 ms default window

    QtUiDispatcher dispatcher;
    app::DeviceListVM listVm(facade, bus, dispatcher);
    app::DeviceDetailVM detailVm(facade);
    app::StatusLineVM statusVm(bus, delayed, dispatcher);

    // Keep every refresh future alive so we can wait on them before teardown —
    // ApplicationFacade::refresh()'s documented lifetime contract.
    std::vector<std::future<void>> pending;

    MainWindow window(listVm, detailVm, statusVm, dispatcher, [&] {
        // Drop already-completed refreshes so `pending` stays bounded.
        std::erase_if(pending, [](const std::future<void>& f) {
            return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
        pending.push_back(facade.refresh());
    });

    int rc = 0;
    // The try widens over hotplug.start()/publish for the same reason as
    // runTuiApp(): live threads exist before exec(), so an exception on any
    // pre-loop call must stop them before the VMs/dispatcher unwind.
    try {
        facade.refresh().wait();
        // The worker's queued rebuild is delivered here, before first paint —
        // the GUI-thread analogue of the TUI's dispatcher.drain().
        QCoreApplication::processEvents();
        statusVm.arm();
        if (auto started = hotplug.start(); !started) {
            // Degrade gracefully: without live events, Refresh still works.
            bus.publish(
                core::ErrorEvent{.source = "hotplug", .message = started.error().message});
        }
        if (selfTest) {
            std::cout << "self-test rows: " << listVm.rowsRef().size() << '\n';
        } else {
            window.show();
            rc = QApplication::exec();
        }
    } catch (...) {
        hotplug.stop();
        delayed.shutdown();
        drainPending(pending);
        throw;
    }

    // Stop event sources before the VMs/dispatcher unwind, then wait for any
    // in-flight refresh — order and rationale identical to runTuiApp().
    hotplug.stop();
    delayed.shutdown();
    drainPending(pending);
    return rc;
}

}  // namespace devmgr::gui
```

`gui/src/main.cpp`:

```cpp
#include "gui/src/gui_app.hpp"

int main(int argc, char** argv) { return devmgr::gui::runGuiApp(argc, argv); }
```

- [ ] **Step 2: Build wiring**

In `gui/CMakeLists.txt`: add `src/gui_app.cpp` to the `devmgr_gui` sources, then append after the test target:

```cmake
add_executable(devmgr-gui src/main.cpp)
target_link_libraries(devmgr-gui PRIVATE devmgr_gui)

# Offscreen launch smoke: full wiring (real PAL) + one enumeration + rebuild.
# In containers the netlink hotplug start may fail — the degrade path still
# exits 0; the smoke asserts wiring, not host capabilities.
add_test(NAME devmgr_gui_selftest COMMAND devmgr-gui --self-test)
set_tests_properties(devmgr_gui_selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
```

- [ ] **Step 3: Run the smoke to verify**

Run: `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --test-dir build/linux-debug -R devmgr_gui_selftest --output-on-failure`
Expected: PASS; the test log contains `self-test rows: N` with N ≥ 1.

- [ ] **Step 4: Full suite + interactive sanity launch**

Run: `ctest --test-dir build/linux-debug --output-on-failure`
Expected: 70 tests, all pass.
Then (host, interactive — skip in a container): `./build/linux-debug/gui/devmgr-gui` — window opens with grouped devices, filter narrows, selecting fills the detail pane, Refresh works, `q`uitting the window exits cleanly (exit code 0).

- [ ] **Step 5: Format gate**

Run: `clang-format --dry-run --Werror gui/src/gui_app.hpp gui/src/gui_app.cpp gui/src/main.cpp`
Expected: clean.

- [ ] **Step 6: USER commits**

```
feat(gui): devmgr-gui composition root — TUI-mirrored teardown ordering + --self-test smoke
```

---

### Task 6: CI, Docker, lint gates, toolkit-purity guard

**Files:**
- Modify: `Dockerfile`
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: everything built in Tasks 1–5.
- Produces: CI that builds the GUI, runs all GUI tests + the selftest offscreen inside the container, format/tidy gates covering `gui/`, and a purity gate that fails the build if any Qt include appears under `core/`, `app/`, or `platform/`.

- [ ] **Step 1: Add Qt to the test image**

In `Dockerfile`, add `qt6-base-dev` to the apt install list (alphabetical slot, before `umockdev`):

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl zip unzip tar pkg-config \
        clang-tidy clang-format ca-certificates \
        libudev-dev libkmod-dev qt6-base-dev umockdev libumockdev-dev \
    && rm -rf /var/lib/apt/lists/*
```

(Ubuntu 24.04's `qt6-base-dev` is Qt 6.4.x — this IS the Qt floor; anything that compiles here compiles everywhere we support. With Qt present, `DEVMGR_BUILD_GUI` defaults ON, so the image's existing `cmake --preset linux-debug && cmake --build` line and the `ctest` CMD pick up the GUI targets, GUI tests, and the offscreen selftest with no compose changes.)

- [ ] **Step 2: Extend the CI gates**

In `.github/workflows/ci.yml`:

1. Format step — add `gui` to the find roots:

```yaml
          find core tests app platform tui gui -name '*.hpp' -o -name '*.cpp' | xargs clang-format --dry-run --Werror
```

2. Insert a purity-guard step between "Check formatting" and "Build and unit-test (Docker)":

```yaml
      - name: Toolkit purity guard (no Qt in core/app/platform)
        # The Phase 3 seam test as a hard gate: the shared layers must stay
        # toolkit-agnostic. `!` inverts grep — finding any match fails the job.
        run: |
          ! grep -rn --include='*.hpp' --include='*.cpp' -E '#include\s*[<"]Q[A-Za-z]' core app platform
```

3. Static-analysis step — add `gui/src/*.cpp` to the clang-tidy invocation:

```yaml
        run: docker compose -f test/docker-compose.yml run --rm unit bash -c "clang-tidy -p build/linux-debug --warnings-as-errors='*' core/src/*.cpp app/src/*.cpp platform/linux/src/*.cpp gui/src/*.cpp"
```

- [ ] **Step 3: Verify the purity guard logic locally (both polarities)**

Run: `! grep -rn --include='*.hpp' --include='*.cpp' -E '#include\s*[<"]Q[A-Za-z]' core app platform && echo GUARD-CLEAN`
Expected: `GUARD-CLEAN`.
Run: `grep -rln --include='*.cpp' -E '#include\s*[<"]Q[A-Za-z]' gui | head -1`
Expected: at least one `gui/` file listed (proves the pattern actually matches Qt includes — the guard isn't vacuously green).

- [ ] **Step 4: Run clang-tidy on gui/src locally and fix findings**

Run: `clang-tidy -p build/linux-debug --warnings-as-errors='*' gui/src/*.cpp`
Expected: no diagnostics. If Qt macro internals (`Q_OBJECT`, `emit`) trigger checks that cannot be satisfied without fighting Qt idiom, suppress with a targeted `// NOLINT(<check-name>)` plus a one-line justification — never blanket-disable a check repo-wide for this.

- [ ] **Step 5: Container parity run (user, rootless podman)**

Hand the user: `podman-compose -f test/docker-compose.yml run --rm unit`
Expected: image rebuilds with Qt, full ctest passes in-container (including `devmgr_gui_tests` and `devmgr_gui_selftest` offscreen).

- [ ] **Step 6: USER commits**

```
ci: build+test Qt GUI in container; format/tidy cover gui/; toolkit-purity guard for core/app/platform
```

---

## Manual parity smoke (user, real host — the phase's exit gate)

Not a task step; run after Task 6, before merging `feature/phase3`:

1. Launch `devmgr-tui` and `devmgr-gui` side by side.
2. Identical device data: same groups, same device counts, same names in both.
3. Plug a USB device: both UIs show it live (≤ 250 ms debounce window later); GUI status bar and TUI status line both show the transient "usb device added: …" then auto-clear (~4 s).
4. Unplug it: both remove the row; selection stays on the previously selected device in both.
5. Type the same filter string in both: identical surviving rows.
6. GUI stays responsive during a Refresh (toolbar) — no freeze.
7. Close both: clean exits, no crash on teardown.

## Deviations from TUI worth knowing (all deliberate, from the spec)

- No detail-pane render cache in the GUI: Qt slots fire on sparse events, not per frame — the TUI's cache exists because FTXUI re-renders on every input event.
- The GUI never calls `dispatcher.drain()` — Qt's event loop delivers posts; `processEvents()` before first paint replaces the TUI's pre-loop `drain()`.
- Selection can never land on a header row in the GUI (model flags), whereas the TUI menu can highlight one (`selectedDeviceId()` → `nullopt` handles both).
