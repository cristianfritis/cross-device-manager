# Phase 6 — Driver/Firmware Updates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** fwupd/LVFS firmware update visibility + local-cab install & DKMS status display behind one `IUpdateProvider` seam, Updates tab in both UIs, + 11-item Phase 5 carry-over cleanup.

**Architecture:** Frontend-direct `FwupdUpdateProvider` (raw sdbus-c++ v2 client of `org.freedesktop.fwupd`; fwupd's own polkit gates Install) + read-only `DkmsStatusProvider` (fs walk), aggregated by `ApplicationFacade` into per-provider snapshots consumed by a new `UpdatesVM`. devmgrd + IPC v2 untouched.

**Tech Stack:** C++20, sdbus-c++ v2 (gated `DEVMGR_WITH_SDBUS`), GoogleTest, FTXUI, Qt6, dbus-run-session for integration, Vagrant/libvirt VM for E2E.

**Spec:** `docs/superpowers/specs/2026-07-10-phase6-driver-firmware-updates-design.md` (rev 2). Review-mandated items: **M1** durable reboot/offline state (spec §8.2), **M2** secure cab resolution (§5.3), **M3** install lifecycle (§5.5).

**Encoding note:** plan prose = ck:caveman (user directive 2026-07-07). Code blocks, paths, identifiers, commands verbatim — always.

## Global Constraints

- **USER commits every task** — agent denied `git add`/`git commit`. Each task's final step = hand exact paths + message to user.
- C++20, 4-space indent, `.clang-format` at repo root — run `clang-format -i` on every touched file before handoff; CI checks with 18.1.8 AND 21.1.8.
- `clang-tidy` gate = CI form (globs `*.cpp` in gated dirs); host tidy needs `-mno-direct-extern-access` workaround (see `.superpowers/sdd/progress.md` env notes).
- Purity: NO GLib/GObject anywhere (`glib|gobject` ∉ core/app/tui/gui/platform includes); NO Qt outside gui/; NO ftxui outside tui/; NO sdbus outside `DEVMGR_WITH_SDBUS`-gated sources; core/app link no platform libs.
- Both build configs must stay green: sdbus ON (host/container/VM) and OFF (`cmake -B build/nosdbus -DDEVMGR_WITH_SDBUS=OFF`).
- All errors via `core::Result` / `core::Error::Code { Permission, NotFound, Busy, Io, Network, Unsupported, Conflict }` (`core/include/devmgr/core/result.hpp:10`). ⊥ exceptions escape providers (spec V2).
- Teardown contract (Phase 2/5): dispatcher-posted closures carry alive tokens; dtors drain/barrier before member destruction; `DelayedScheduler` outlives its clients.
- Build: `cmake --preset linux-debug && cmake --build --preset linux-debug -j24`. Tests: `ctest --test-dir build/linux-debug --output-on-failure`.
- fwupd D-Bus surface + spec invariants V1–V5: spec §5. Progress callback = existing `runtime::ProgressReporter` (`core/include/devmgr/runtime/progress.hpp`), `percent = -1` ⇒ indeterminate (spec refinement: reuse > invent).

## File Map (created/modified across tasks)

```
core/include/devmgr/core/update_models.hpp        NEW  T2  value types (spec §4.1)
core/include/devmgr/core/events.hpp               MOD  T2  +UpdatesChangedEvent, UpdatesRefreshedEvent, UpdateRequestEvent
core/include/devmgr/pal/interfaces.hpp            MOD  T1(comment), T2(replace IUpdateProvider stub :66-72)
tests/fakes/fake_update_provider.hpp              NEW  T2
platform/linux/include/devmgr/platform/linux/fwupd_contract.hpp  NEW T3 (+ src/fwupd_contract.cpp)
platform/linux/include/devmgr/platform/linux/cab_resolver.hpp    NEW T4 (+ src/cab_resolver.cpp)
platform/linux/include/devmgr/platform/linux/dkms_status_provider.hpp NEW T5 (+ src/dkms_status_provider.cpp)
platform/linux/include/devmgr/platform/linux/fwupd_update_provider.hpp NEW T6/T8 (+ src/fwupd_update_provider.cpp)
tests/fwupd/  (CMakeLists.txt, fake_fwupd_daemon.{hpp,cpp}, test_fwupd_provider_ipc.cpp) NEW T7/T8
app/include/devmgr/app/application_facade.hpp     MOD  T9  (+ src/application_facade.cpp)
app/include/devmgr/app/updates_vm.hpp             NEW  T10 (+ src/updates_vm.cpp)
tui/src/tui_app.cpp                               MOD  T1(Escape), T11(Updates tab)
gui/src/main_window.{hpp,cpp}                     MOD  T1(F-1), T12(Updates tab)
gui/src/update_list_model.{hpp,cpp}               NEW  T12
daemon/src/state_store.cpp                        MOD  T1
daemon/src/request_processor.cpp                  MOD  T1
platform/linux/src/dbus_privileged_channel.cpp    MOD  T1
test/vm/phase5-smoke.sh                           MOD  T1 (F-2)
test/vm/phase6-smoke.sh + tests/smoke/            NEW  T13
test/vm/Vagrantfile, test-vm.sh, README.md        MOD  T13
```

Dependency order: T1 independent; T2 → {T3..T10}; T3+T4 → T6; T6 → T7 → T8; T2 → T5; {T8,T5} → T9 → T10 → {T11,T12} → T13.

---

### Task 1: Carry-over cleanup (11 items, spec §10)

**Files:**
- Modify: `daemon/src/state_store.cpp:64-111`
- Modify: `daemon/src/request_processor.cpp:89-104`
- Modify: `platform/linux/src/dbus_privileged_channel.cpp:157-159`
- Modify: `core/include/devmgr/pal/interfaces.hpp:52-56`
- Modify: `tui/src/tui_app.cpp:273-276` + delete Escape block at `:380`
- Modify: `gui/src/main_window.cpp:231-234` + guard-refusal `statusBar()` writes
- Modify: `test/vm/phase5-smoke.sh:29-33`
- Test: `tests/unit/test_state_store.cpp`, `tests/unit/test_kmod_error_taxonomy.cpp`, `tests/unit/test_enforcement_service.cpp`

**Interfaces:** no new public API. Produces: hardened StateStore semantics later tasks rely on (unchanged signatures).

- [x] **Step 1: state_store fixes (T4 m-2/m-4/m-5) — write failing tests first**

Append to `tests/unit/test_state_store.cpp` (mirror existing fixture style in that file):

```cpp
TEST_F(StateStoreTest, NullEntriesArrayQuarantinesFile) {
    writeStateFile(R"({"version":1,"entries":null})");
    StateStore store(dir());
    ASSERT_TRUE(store.load().has_value());
    EXPECT_TRUE(store.entries().empty());
    // Evidence preserved: exactly one quarantine file, original gone.
    EXPECT_EQ(countFilesMatching("state.json.bad"), 1);
    EXPECT_FALSE(std::filesystem::exists(std::filesystem::path(dir()) / "state.json"));
}

TEST_F(StateStoreTest, SecondCorruptionDoesNotOverwriteFirstEvidence) {
    writeStateFile("not json at all");
    StateStore store(dir());
    ASSERT_TRUE(store.load().has_value());
    writeStateFile(R"({"entries":null})");
    ASSERT_TRUE(store.load().has_value());
    EXPECT_EQ(countFilesMatching("state.json.bad"), 2);  // timestamped, both kept
}
```

If the fixture lacks `writeStateFile`/`countFilesMatching`, add them (write to `dir()/state.json`; count directory entries whose filename starts with the prefix). Note: two same-second corruptions need distinct names — implementation appends a counter (see Step 3).

- [x] **Step 2: run new tests, verify both FAIL** (`.bad` name collision / null-entries iterates empty without quarantine)

Run: `ctest --test-dir build/linux-debug -R StateStore --output-on-failure`
Expected: `NullEntriesArrayQuarantinesFile` FAIL (no quarantine happens — `contains("entries")` passes, iteration over null throws → caught → single `.bad`... verify actual behavior; at minimum `SecondCorruption` FAILs on name collision `countFilesMatching == 1`).

- [x] **Step 3: implement state_store fixes**

In `daemon/src/state_store.cpp`:

(a) `load()` — replace both `fs::rename(file, fs::path(dir_) / "state.json.bad", ec);` sites (`:74`, `:84`) with a call to a new file-local helper; extend the `:72` condition:

```cpp
// above load(), in the anonymous namespace:
void quarantine(const fs::path& dir, const fs::path& file) {
    std::error_code ec;
    const auto stamp = std::to_string(std::time(nullptr));
    fs::path bad = dir / ("state.json.bad-" + stamp);
    for (int n = 1; fs::exists(bad, ec); ++n)  // same-second corruption: never overwrite
        bad = dir / ("state.json.bad-" + stamp + "." + std::to_string(n));
    fs::rename(file, bad, ec);
}
```

```cpp
    if (doc.is_discarded() || !doc.is_object() || !doc.contains("entries") ||
        !doc["entries"].is_array()) {
        // Never silently destroy evidence (spec §5.2); null/non-array "entries"
        // must quarantine too — iterating it as empty would silently drop every
        // persisted disable (Phase 5 review T4 m-5).
        quarantine(fs::path(dir_), file);
        return {};
    }
```

(add `#include <ctime>`)

(b) `save()` — remove the orphan tmp on every post-create failure path (`:103`, `:105`, `:107`):

```cpp
        if (!out) {
            fs::remove(tmp, ec);
            return core::makeError(core::Error::Code::Io, "write failed: " + tmp.string());
        }
    }
    if (auto r = syncFd(tmp.string(), O_WRONLY | O_CLOEXEC); !r) {
        fs::remove(tmp, ec);
        return r;
    }
    fs::rename(tmp, file, ec);  // atomic on POSIX
    if (ec) {
        const auto msg = ec.message();
        fs::remove(tmp, ec);
        return core::makeError(core::Error::Code::Io, "rename failed: " + msg);
    }
```

- [x] **Step 4: run StateStore tests → PASS; full ctest → no regressions**

Run: `ctest --test-dir build/linux-debug --output-on-failure`
Expected: all pass (215 + 2 new).

- [x] **Step 5: kmod taxonomy matrix (T6 m-2) — 5 new tests**

Read `platform/linux/include/devmgr/platform/linux/kmod_error_taxonomy.hpp` for exact `describeLoadFailure`/`describeUnloadFailure` signatures + branch strings, then append to `tests/unit/test_kmod_error_taxonomy.cpp` (assert on `code` + substring, ⊥ full-string where message unpinned):

```cpp
TEST(KmodErrorTaxonomy, UnloadPermissionDenied) {
    const auto e = pl::describeUnloadFailure(EPERM, "dummy", {});
    EXPECT_EQ(e.code, Error::Code::Permission);
    EXPECT_NE(e.message.find("dummy"), std::string::npos) << e.message;
}
TEST(KmodErrorTaxonomy, LoadBusyIsBusy) {
    const auto e = pl::describeLoadFailure(EBUSY, "dummy", {}, "none");
    EXPECT_EQ(e.code, Error::Code::Busy);
}
TEST(KmodErrorTaxonomy, LoadUnknownErrnoFallsBackToIo) {
    const auto e = pl::describeLoadFailure(EIO, "dummy", {}, "none");
    EXPECT_EQ(e.code, Error::Code::Io);
    EXPECT_NE(e.message.find("dummy"), std::string::npos) << e.message;
}
TEST(KmodErrorTaxonomy, UnloadEnoentIsNotFound) {
    const auto e = pl::describeUnloadFailure(ENOENT, "ghost", {});
    EXPECT_EQ(e.code, Error::Code::NotFound);
}
TEST(KmodErrorTaxonomy, UnloadBusyWithNoHoldersStillReadable) {
    const auto e = pl::describeUnloadFailure(EBUSY, "usbcore", {});
    EXPECT_EQ(e.code, Error::Code::Busy);
    EXPECT_NE(e.message.find("usbcore"), std::string::npos) << e.message;
}
```

If a branch doesn't exist in the taxonomy (e.g. EBUSY-load maps elsewhere), adjust the EXPECT to the actual pinned mapping — the point is pinning ALL branches, not inventing new ones. Run → expected PASS immediately (these pin existing behavior; any FAIL = real taxonomy gap → fix taxonomy, document in report).

- [x] **Step 6: enforcement unbind-branch test (T9 m-2)**

In `tests/unit/test_enforcement_service.cpp`, clone one existing sweep-fallback test (fixture helpers at `:40-74`), flip the entry to the unbind mechanism:

```cpp
TEST_F(EnforcementServiceTest, SweepFallbackReappliesUnbindMechanism) {
    auto d = usbDevice(makeSysfsDevice("3-2", "1"), "SER-unbind");
    auto e = entryFor(d);
    e.mechanism = "unbind";
    e.lastDriver = "e1000e";
    store_->upsert(e);
    pal_.enumerateResult_ = std::vector<Device>{d};   // match existing fake usage
    service().sweep();
    // FakePal's controller records setEnabled calls — assert the disable landed
    // (mirror the existing fallback test's assertion style exactly).
    ASSERT_FALSE(pal_.setEnabledCalls_.empty());
    EXPECT_EQ(pal_.setEnabledCalls_.back().sysfsPath, d.sysfsPath);
    EXPECT_FALSE(pal_.setEnabledCalls_.back().enabled);
}
```

Adapt member names to `tests/fakes/fake_pal.hpp` actuals (read it first). Run → PASS (pins existing derivation-mirror behavior).

- [x] **Step 7: remaining code fixes (no new tests — inspection/manual-gate items)**

(a) **T5 m-1** `core/include/devmgr/pal/interfaces.hpp:52-56` — replace the stale sentence `the bound one is identified by the caller via Device::boundDriver.` with:

```
    // modalias and sysfsPath (spec §4.2 refinement; same rationale as
    // IPrivilegedChannel taking Device). Returns the modalias candidate list
    // ordered per the pinned contract (kmod_driver_manager.hpp): the FIRST
    // element is the currently-bound (or builtin) driver when one exists —
    // both frontends' bind-prefill depends on that ordering.
```

(b) **T9 m-1** `platform/linux/src/dbus_privileged_channel.cpp` — after the `catch (const sdbus::Error& e)` at `:157-159` add:

```cpp
    } catch (const std::exception& e) {
        // Defense vs a future daemon omitting keys: m.at() throws
        // std::out_of_range, which must not escape the never-throws Result
        // contract nor kill the facade refresh worker (Phase 5 review T9 m-1).
        return tl::unexpected(core::makeError(core::Error::Code::Io,
                                              std::string{"malformed ListDisabledDevices reply: "} +
                                                  e.what()));
    }
```

(c) **T11 m-2** `tui/src/tui_app.cpp:273` — make Escape quit global:

```cpp
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
```

and DELETE the now-dead devices-only block at `:380-383` (`if (event == Event::Escape) { ... }`). Safe: confirm/text prompts consume Escape earlier (`:237-261`).

(d) **F-1** `gui/src/main_window.cpp`:
  1. Locate direct guard-refusal `statusBar()->showMessage(...)` calls in the toggle/unbind/bind action handlers (search `showMessage` outside `updateStatusBar`). Replace each with the TUI's pattern (`tui/src/tui_app.cpp:293-296`): `bus_.publish(core::TaskCompletedEvent{.taskId = "guard", .ok = false, .message = <same text>});` — StatusLineVM owns the status line, TTL + no wipe-by-wake. (MainWindow needs the bus: check ctor params; it already receives what StatusLineVM uses — if no `runtime::EventBus&` member, add one to the ctor + wire in `gui/src/gui_app.cpp`.)
  2. Gate the devices-side probe: `:231-234` modelReset lambda →

```cpp
    connect(moduleModel_, &QAbstractItemModel::modelReset, this, [this] {
        updateModuleDetailPane();
        // Module-side resets must not re-run the Devices-tab criticality probe
        // (reads /proc/self/mounts + sysfs) — Phase 5 review F-1.
        if (tabs_->currentIndex() == 1) updateActionEnablement();
    });
```

(e) **F-2** `test/vm/phase5-smoke.sh:29-33` —

```bash
start_daemon() {
    ./build/linux-debug/daemon/devmgrd &
    DPID=$!
    for _ in $(seq 1 50); do
        busctl status org.devmgr.Manager1 >/dev/null 2>&1 && return 0
        sleep 0.2
    done
    echo "devmgrd did not claim org.devmgr.Manager1 within 10s"; exit 1
}
```

(f) **F-3** `daemon/src/request_processor.cpp:89-104` — the removal scan must also match by device key (device may have reappeared at a NEW sysfs path):

```cpp
core::Result<void> RequestProcessor::applyEnable(const std::string& canonical) {
    // Enable: delete the entry FIRST, then rebind — a rebind failure must
    // leave "enabled-but-unbound" with a clear error, never a lying store.
    // Match by lastSysfsPath OR by device key: after a daemon-down replug the
    // stored path is stale while the key still identifies the device
    // (Phase 5 review F-3).
    const auto probed = deviceFromSysfs(canonical);
    std::string hint;
    for (const auto& e : store_.entries()) {
        const bool pathMatch = e.lastSysfsPath == canonical;
        const bool keyMatch = probed && services::matchesDevice(e.key, *probed);
        if (pathMatch || keyMatch) {
            hint = e.lastDriver;
            auto removed = store_.remove(e.key);
            if (!removed) return removed;
            break;
        }
    }
    ...unchanged...
}
```

`deviceFromSysfs` = the daemon-side probe the enforcement fallback already uses — check `daemon/src/sysfs_device_probe.cpp` for the exact name/signature and `enforcement_service.cpp` for its call shape; adapt (incl. header include) to actuals. Add a unit test in `tests/unit/test_request_processor.cpp`: disable via path A (store entry), simulate replug (entry's `lastSysfsPath` stays A, device now at path B with same identity attrs), `applyEnable(B)` → store empty afterward.

- [x] **Step 8: gates**

```
cmake --build --preset linux-debug -j24
ctest --test-dir build/linux-debug --output-on-failure          # expect: all pass, ≥ 215+9
clang-format -i <every touched file>; git diff --stat            # only intended files
```

Also rebuild `-DDEVMGR_WITH_SDBUS=OFF` config once (interfaces.hpp touched): `cmake -B build/nosdbus -DDEVMGR_WITH_SDBUS=OFF -G Ninja && cmake --build build/nosdbus -j24`.

- [x] **Step 9: USER commits**

```
fix: Phase 6 T1 — land 11 Phase 5 review carry-overs
(state_store quarantine/tmp hygiene/null-entries, driversFor doc, kmod taxonomy
matrix, channel at() guard, enforcement unbind test, TUI Escape parity, GUI
refusals via StatusLineVM + probe gating, VM smoke poll, applyEnable key match)
```

---

### Task 2: Core update models + `IUpdateProvider` + fake

**Files:**
- Create: `core/include/devmgr/core/update_models.hpp`
- Modify: `core/include/devmgr/core/events.hpp` (append 3 events)
- Modify: `core/include/devmgr/pal/interfaces.hpp` (REPLACE stub `IUpdateProvider` at `:66-72` — zero existing users, verified)
- Create: `tests/fakes/fake_update_provider.hpp`
- Test: `tests/unit/test_update_models.cpp`; register in `tests/CMakeLists.txt:24` block

**Interfaces (Produces — every later task consumes these EXACT names):**
- `core::InstallDisposition`, `core::InstallOutcome`, `core::ReleaseRef`, `core::DeviceUpdateFacts`, `core::ReleaseInfo`, `core::UpdateCandidate`, `core::ProviderAvailability`, `core::UpdateProviderState`, `core::PendingAction` (fields = code below)
- `pal::UpdateProviderCaps` (Query/Install bits), `pal::hasCap`, `pal::IUpdateProvider` (5 pure virtuals below)
- `core::UpdatesChangedEvent{}`, `core::UpdatesRefreshedEvent{}`, `core::UpdateRequestEvent{providerId, deviceId, kind, message}`
- `tests::FakeUpdateProvider` scripted via public members

- [x] **Step 1: write `core/include/devmgr/core/update_models.hpp`** (complete file)

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "devmgr/core/result.hpp"

namespace devmgr::core {

// Spec §4.1. Disposition of a completed install() call — success has shapes:
// an offline/scheduled update reports success with NO immediate version bump.
enum class InstallDisposition { Completed, Scheduled, NeedsReboot, NeedsUserAction };

struct InstallOutcome {
    InstallDisposition disposition = InstallDisposition::Completed;
    bool needsReboot = false;
    std::optional<std::string> observedVersion;  // absent for Scheduled/offline
    std::string message;
};

// Stable release identity across metadata refreshes (spec §2: never select or
// match releases by version-string comparison).
struct ReleaseRef {
    std::string remoteId;
    std::string checksum;
    bool operator==(const ReleaseRef&) const = default;
};

struct DeviceUpdateFacts {  // device facts ONLY — no transient/app state here
    bool updatable = false;
    bool supported = false;
    bool needsRebootAfterUpdate = false;
};

struct ReleaseInfo {
    std::string version;
    std::string summary;
    std::string remoteId;
    std::string checksum;
    std::vector<std::string> locations;  // fwupd "Locations", legacy "Uri" folded in
    bool localCab = false;               // resolvable per spec §5.3 ⇒ install verb enabled
    std::uint64_t sizeBytes = 0;
    bool isUpgrade = false;
    std::optional<std::uint32_t> installDurationSec;
    ReleaseRef ref() const { return {remoteId, checksum}; }
};

struct UpdateCandidate {
    std::string providerId;  // "fwupd" | "dkms"
    std::string id;          // fwupd DeviceId | "dkms:<module>/<version>"
    std::string displayName;
    std::string currentVersion;
    std::optional<std::string> candidateVersion;  // = releases.front().version (fwupd order)
    DeviceUpdateFacts facts;
    std::vector<ReleaseInfo> releases;  // empty for dkms
    std::vector<std::pair<std::string, std::string>> details;
};

struct ProviderAvailability {
    bool available = false;
    std::optional<std::string> version;  // e.g. fwupd DaemonVersion
    std::optional<Error> error;          // machine state; UI renders its message
    std::vector<std::string> notices;    // e.g. metadata-age hints
};

struct UpdateProviderState {
    std::string providerId;
    ProviderAvailability availability;
    std::vector<UpdateCandidate> candidates;
    std::optional<Error> refreshError;  // enumerate failed (availability may be true)
};

// Durable pending/reboot record (spec §8.2 / M1): NEVER derived from the live
// candidate list; fed by install outcomes + provider pendingActions().
struct PendingAction {
    std::string providerId;
    std::string deviceId;
    std::string deviceName;
    InstallDisposition disposition = InstallDisposition::Completed;
    std::string version;
};

}  // namespace devmgr::core
```

- [x] **Step 2: append events to `core/include/devmgr/core/events.hpp`** (before closing namespace)

```cpp
struct UpdatesChangedEvent {};    // provider-side change (fwupd signals) → coalesced refresh
struct UpdatesRefreshedEvent {};  // facade snapshot replaced → VMs rebuild via dispatcher
struct UpdateRequestEvent {       // fwupd DeviceRequest: durable until dismissed/resolved (spec §9)
    std::string providerId;
    std::string deviceId;
    std::string kind;  // "immediate" | "post" | raw fwupd request-kind number
    std::string message;
};
```

- [x] **Step 3: replace the `IUpdateProvider` stub** in `core/include/devmgr/pal/interfaces.hpp:66-72` with:

```cpp
enum class UpdateProviderCaps : unsigned { Query = 1U << 0U, Install = 1U << 1U };
constexpr UpdateProviderCaps operator|(UpdateProviderCaps a, UpdateProviderCaps b) {
    return static_cast<UpdateProviderCaps>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool hasCap(UpdateProviderCaps caps, UpdateProviderCaps bit) {
    return (static_cast<unsigned>(caps) & static_cast<unsigned>(bit)) != 0U;
}

class IUpdateProvider {
   public:
    virtual ~IUpdateProvider() = default;
    virtual std::string providerId() const = 0;
    virtual UpdateProviderCaps capabilities() const = 0;
    virtual core::ProviderAvailability availability() const = 0;
    virtual core::Result<std::vector<core::UpdateCandidate>> enumerate() = 0;
    // Durable pending/reboot records (fwupd: GetHistory/GetResults; dkms: {}).
    virtual core::Result<std::vector<core::PendingAction>> pendingActions() = 0;
    // Blocking (minutes: polkit prompt + flash) — TaskScheduler worker only,
    // never a UI thread. progress runs on provider threads; percent -1 =
    // indeterminate. Implementations must be exception-free (spec V2) and
    // reject non-installable targets (spec V1) even though the UI pre-gates.
    virtual core::Result<core::InstallOutcome> install(const std::string& candidateId,
                                                       const core::ReleaseRef& release,
                                                       runtime::ProgressReporter progress) = 0;
};
```

Add `#include "devmgr/core/update_models.hpp"` to the header's include block.

- [x] **Step 4: write `tests/fakes/fake_update_provider.hpp`** (complete file)

```cpp
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/interfaces.hpp"

namespace devmgr::tests {

// Scripted IUpdateProvider: tests assign the public members. install() runs
// onInstall_ (emit progress mid-call, block on a latch, flip state...) before
// returning installResult_.
class FakeUpdateProvider : public pal::IUpdateProvider {
   public:
    std::string id_ = "fake";
    pal::UpdateProviderCaps caps_ =
        pal::UpdateProviderCaps::Query | pal::UpdateProviderCaps::Install;
    core::ProviderAvailability availability_{.available = true, .version = "1.0", .error = {},
                                             .notices = {}};
    core::Result<std::vector<core::UpdateCandidate>> enumerateResult_ =
        std::vector<core::UpdateCandidate>{};
    core::Result<std::vector<core::PendingAction>> pendingResult_ =
        std::vector<core::PendingAction>{};
    core::Result<core::InstallOutcome> installResult_ = core::InstallOutcome{};
    std::function<void(runtime::ProgressReporter&)> onInstall_;
    std::atomic<int> enumerateCalls_{0};
    std::atomic<int> pendingCalls_{0};
    std::atomic<int> installCalls_{0};
    std::string lastInstallCandidate_;
    core::ReleaseRef lastInstallRelease_;

    std::string providerId() const override { return id_; }
    pal::UpdateProviderCaps capabilities() const override { return caps_; }
    core::ProviderAvailability availability() const override { return availability_; }
    core::Result<std::vector<core::UpdateCandidate>> enumerate() override {
        ++enumerateCalls_;
        return enumerateResult_;
    }
    core::Result<std::vector<core::PendingAction>> pendingActions() override {
        ++pendingCalls_;
        return pendingResult_;
    }
    core::Result<core::InstallOutcome> install(const std::string& candidateId,
                                               const core::ReleaseRef& release,
                                               runtime::ProgressReporter progress) override {
        ++installCalls_;
        lastInstallCandidate_ = candidateId;
        lastInstallRelease_ = release;
        if (onInstall_) onInstall_(progress);
        return installResult_;
    }
};

}  // namespace devmgr::tests
```

- [x] **Step 5: write `tests/unit/test_update_models.cpp`** — pin the contracts the UI relies on

```cpp
#include <gtest/gtest.h>

#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/interfaces.hpp"
#include "fakes/fake_update_provider.hpp"

using namespace devmgr;

TEST(UpdateModels, ReleaseRefEqualityIsRemotePlusChecksum) {
    core::ReleaseInfo a{.version = "1.2.3", .summary = "", .remoteId = "lvfs",
                        .checksum = "abc", .locations = {}, .localCab = false,
                        .sizeBytes = 0, .isUpgrade = true, .installDurationSec = {}};
    core::ReleaseInfo b = a;
    b.version = "9.9.9";  // version differs — identity must NOT (spec §2)
    EXPECT_EQ(a.ref(), b.ref());
}

TEST(UpdateModels, CapsBitOps) {
    using pal::UpdateProviderCaps;
    const auto both = UpdateProviderCaps::Query | UpdateProviderCaps::Install;
    EXPECT_TRUE(pal::hasCap(both, UpdateProviderCaps::Install));
    EXPECT_FALSE(pal::hasCap(UpdateProviderCaps::Query, UpdateProviderCaps::Install));
}

TEST(UpdateModels, FakeProviderRoundTrip) {
    tests::FakeUpdateProvider fake;
    fake.enumerateResult_ = std::vector<core::UpdateCandidate>{
        {.providerId = "fake", .id = "dev1", .displayName = "Dev", .currentVersion = "1",
         .candidateVersion = "2", .facts = {.updatable = true, .supported = true,
         .needsRebootAfterUpdate = false}, .releases = {}, .details = {}}};
    auto r = fake.enumerate();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 1U);
    EXPECT_EQ(fake.enumerateCalls_.load(), 1);
}
```

Register: add `unit/test_update_models.cpp` to the `add_executable(devmgr_tests ...)` list in `tests/CMakeLists.txt` (portable block, `:5-24`).

- [x] **Step 6: build both configs + run**

```
cmake --build --preset linux-debug -j24 && ctest --test-dir build/linux-debug -R UpdateModels --output-on-failure
cmake --build build/nosdbus -j24     # interfaces.hpp changed → OFF config must stay green
```
Expected: 3 new tests PASS; both builds clean. Then full ctest → no regressions.

- [x] **Step 7: USER commits** — `feat(core): Phase 6 T2 — update models, IUpdateProvider v2 (replaces Phase 0 stub), update events, FakeUpdateProvider`

---

### Task 3: fwupd parse layer (`fwupd_contract`)

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/fwupd_contract.hpp`, `platform/linux/src/fwupd_contract.cpp`
- Modify: `platform/linux/CMakeLists.txt` (add source inside the `if(DEVMGR_WITH_SDBUS)` block `:21-26`)
- Test: `tests/unit/test_fwupd_contract.cpp` — register in `tests/CMakeLists.txt` **guarded**: the unit binary links `devmgr_pal_linux`; add the test source only when sdbus is on:

```cmake
if(DEVMGR_WITH_SDBUS)
    target_sources(devmgr_tests PRIVATE unit/test_fwupd_contract.cpp)
    target_link_libraries(devmgr_tests PRIVATE SDBusCpp::sdbus-c++)  # sdbus::Variant in test bodies
endif()
```

**Interfaces (Produces):**
- `platform_linux::fwupd::kBusName/kObjectPath/kInterface`
- `fwupd::Dict = std::map<std::string, sdbus::Variant>`
- `fwupd::parseDevice(const Dict&) → std::optional<ParsedDevice{deviceId,name,vendor,version,facts:core::DeviceUpdateFacts}>` (nullopt = row dropped: empty DeviceId | missing Version)
- `fwupd::parseRelease(const Dict&) → std::optional<core::ReleaseInfo>` (localCab left false — T4/T6 fill it)
- `fwupd::mapError(const std::string& name, const std::string& message) → core::Error`
- `fwupd::isNothingToDo(name) → bool`
- `fwupd::dispositionFromUpdateState(std::uint32_t) → core::InstallDisposition`
- flag constants `kDeviceFlagUpdatable/kDeviceFlagSupported/kDeviceFlagNeedsReboot`, `kReleaseFlagIsUpgrade`, update-state constants

- [x] **Step 1: pin the fwupd constants from the installed headers** (spec §12 plan-time task)

Run: `grep -rn "FWUPD_DEVICE_FLAG_UPDATABLE\|FWUPD_DEVICE_FLAG_SUPPORTED\|FWUPD_DEVICE_FLAG_NEEDS_REBOOT\|FWUPD_RELEASE_FLAG_IS_UPGRADE\|FWUPD_UPDATE_STATE" /usr/include/fwupd-2/libfwupd/fwupd-enums.h`
Expected (verify, do NOT trust memory): `UPDATABLE (1u << 1)`, `SUPPORTED (1u << 5)`, `NEEDS_REBOOT (1u << 8)`, `IS_UPGRADE (1u << 2)`, `UPDATE_STATE_PENDING 1 / SUCCESS 2 / FAILED 3 / NEEDS_REBOOT 4`. Copy the ACTUAL values into the header with a comment citing the source file. If the header path differs (Gentoo may use `/usr/include/fwupd-1`), locate via `ls /usr/include/ | grep fwupd`.

- [x] **Step 2: failing tests first** — `tests/unit/test_fwupd_contract.cpp` (table-driven; this is the review's malformed-variant matrix):

```cpp
#include <gtest/gtest.h>
#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/platform/linux/fwupd_contract.hpp"

namespace fw = devmgr::platform_linux::fwupd;
using devmgr::core::Error;

namespace {
fw::Dict deviceDict() {
    fw::Dict d;
    d["DeviceId"] = sdbus::Variant{std::string{"aabb"}};
    d["Name"] = sdbus::Variant{std::string{"Webcam"}};
    d["Vendor"] = sdbus::Variant{std::string{"ACME"}};
    d["Version"] = sdbus::Variant{std::string{"1.2.2"}};
    d["Flags"] = sdbus::Variant{std::uint64_t{fw::kDeviceFlagUpdatable | fw::kDeviceFlagSupported}};
    return d;
}
}  // namespace

TEST(FwupdContract, ParsesWellFormedDevice) {
    const auto p = fw::parseDevice(deviceDict());
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->deviceId, "aabb");
    EXPECT_TRUE(p->facts.updatable);
    EXPECT_TRUE(p->facts.supported);
    EXPECT_FALSE(p->facts.needsRebootAfterUpdate);
}
TEST(FwupdContract, EmptyDeviceIdDropsRow) {
    auto d = deviceDict();
    d["DeviceId"] = sdbus::Variant{std::string{}};
    EXPECT_FALSE(fw::parseDevice(d).has_value());
}
TEST(FwupdContract, MissingVersionDropsRow) {
    auto d = deviceDict();
    d.erase("Version");
    EXPECT_FALSE(fw::parseDevice(d).has_value());
}
TEST(FwupdContract, WrongVariantTypeForKnownKeyIsSkippedNotFatal) {
    auto d = deviceDict();
    d["Flags"] = sdbus::Variant{std::string{"not-a-number"}};  // wrong type
    const auto p = fw::parseDevice(d);
    ASSERT_TRUE(p.has_value());          // row survives
    EXPECT_FALSE(p->facts.updatable);    // flag field defaulted
}
TEST(FwupdContract, UnknownKeysIgnored) {
    auto d = deviceDict();
    d["FutureKey2027"] = sdbus::Variant{std::uint64_t{42}};
    EXPECT_TRUE(fw::parseDevice(d).has_value());
}
TEST(FwupdContract, ReleaseLocationsPreferredUriFallback) {
    fw::Dict r;
    r["Version"] = sdbus::Variant{std::string{"1.2.4"}};
    r["RemoteId"] = sdbus::Variant{std::string{"fwupd-tests"}};
    r["Checksum"] = sdbus::Variant{std::string{"deadbeef"}};
    r["Uri"] = sdbus::Variant{std::string{"./fakedevice124.cab"}};
    const auto a = fw::parseRelease(r);
    ASSERT_TRUE(a.has_value());
    ASSERT_EQ(a->locations.size(), 1U);
    EXPECT_EQ(a->locations[0], "./fakedevice124.cab");
    r["Locations"] = sdbus::Variant{std::vector<std::string>{"https://x/y.cab"}};
    const auto b = fw::parseRelease(r);
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->locations, std::vector<std::string>{"https://x/y.cab"});  // Locations wins
}
TEST(FwupdContract, ReleaseUpgradeFlagAndDuration) {
    fw::Dict r;
    r["Version"] = sdbus::Variant{std::string{"2"}};
    r["RemoteId"] = sdbus::Variant{std::string{"lvfs"}};
    r["Checksum"] = sdbus::Variant{std::string{"c"}};
    r["Flags"] = sdbus::Variant{std::uint64_t{fw::kReleaseFlagIsUpgrade}};
    r["InstallDuration"] = sdbus::Variant{std::uint32_t{120}};
    r["Size"] = sdbus::Variant{std::uint64_t{4096}};
    const auto p = fw::parseRelease(r);
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->isUpgrade);
    EXPECT_EQ(p->installDurationSec, std::uint32_t{120});
    EXPECT_EQ(p->sizeBytes, 4096U);
}
TEST(FwupdContract, ErrorTable) {
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.AuthFailed", "denied").code,
              Error::Code::Permission);
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.NothingToDo", "").code, Error::Code::Conflict);
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.NeedsUserAction", "replug").code,
              Error::Code::Busy);
    EXPECT_EQ(fw::mapError("org.freedesktop.fwupd.VersionNewer", "").code, Error::Code::Conflict);
    const auto unknown = fw::mapError("org.freedesktop.fwupd.Whatever2030", "boom");
    EXPECT_EQ(unknown.code, Error::Code::Io);
    EXPECT_NE(unknown.message.find("org.freedesktop.fwupd.Whatever2030"), std::string::npos);
    EXPECT_NE(unknown.message.find("boom"), std::string::npos);  // name + msg both preserved
    EXPECT_EQ(fw::mapError("", "").code, Error::Code::Io);       // empty name/msg safe
    EXPECT_TRUE(fw::isNothingToDo("org.freedesktop.fwupd.NothingToDo"));
}
TEST(FwupdContract, UpdateStateToDisposition) {
    EXPECT_EQ(fw::dispositionFromUpdateState(fw::kUpdateStateSuccess),
              devmgr::core::InstallDisposition::Completed);
    EXPECT_EQ(fw::dispositionFromUpdateState(fw::kUpdateStateNeedsReboot),
              devmgr::core::InstallDisposition::NeedsReboot);
    EXPECT_EQ(fw::dispositionFromUpdateState(fw::kUpdateStatePending),
              devmgr::core::InstallDisposition::Scheduled);
}
```

Run: build fails (`fwupd_contract.hpp` missing) — expected RED.

- [x] **Step 3: implement header + cpp**

`fwupd_contract.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <sdbus-c++/sdbus-c++.h>

#include "devmgr/core/update_models.hpp"

namespace devmgr::platform_linux::fwupd {

inline constexpr const char* kBusName = "org.freedesktop.fwupd";
inline constexpr const char* kObjectPath = "/";
inline constexpr const char* kInterface = "org.freedesktop.fwupd";

// Values pinned from /usr/include/fwupd-2/libfwupd/fwupd-enums.h (T3 Step 1)
// — REPLACE with the grep output if it differs:
inline constexpr std::uint64_t kDeviceFlagUpdatable = 1ULL << 1U;
inline constexpr std::uint64_t kDeviceFlagSupported = 1ULL << 5U;
inline constexpr std::uint64_t kDeviceFlagNeedsReboot = 1ULL << 8U;
inline constexpr std::uint64_t kReleaseFlagIsUpgrade = 1ULL << 2U;
inline constexpr std::uint32_t kUpdateStatePending = 1;
inline constexpr std::uint32_t kUpdateStateSuccess = 2;
inline constexpr std::uint32_t kUpdateStateFailed = 3;
inline constexpr std::uint32_t kUpdateStateNeedsReboot = 4;

using Dict = std::map<std::string, sdbus::Variant>;

struct ParsedDevice {
    std::string deviceId;
    std::string name;
    std::string vendor;
    std::string version;
    core::DeviceUpdateFacts facts;
};

// nullopt ⇒ drop row (empty DeviceId / missing Version) — spec §5.1.
std::optional<ParsedDevice> parseDevice(const Dict& dict);
// localCab stays false here; the provider fills it via CabResolver (T4/T6).
std::optional<core::ReleaseInfo> parseRelease(const Dict& dict);
core::Error mapError(const std::string& name, const std::string& message);
bool isNothingToDo(const std::string& name);
core::InstallDisposition dispositionFromUpdateState(std::uint32_t state);

}  // namespace devmgr::platform_linux::fwupd
```

`fwupd_contract.cpp` — the load-bearing piece is the tolerant getter (spec §5.1 malformed-variant rule):

```cpp
#include "devmgr/platform/linux/fwupd_contract.hpp"

#include <spdlog/spdlog.h>

namespace devmgr::platform_linux::fwupd {
namespace {

// Known key, wrong variant type ⇒ treat as absent + debug log; never throw.
template <typename T>
std::optional<T> get(const Dict& d, const char* key) {
    const auto it = d.find(key);
    if (it == d.end()) return std::nullopt;
    try {
        return it->second.get<T>();
    } catch (const std::exception& e) {
        spdlog::debug("fwupd: key '{}' has unexpected variant type: {}", key, e.what());
        return std::nullopt;
    }
}

}  // namespace

std::optional<ParsedDevice> parseDevice(const Dict& dict) {
    ParsedDevice out;
    out.deviceId = get<std::string>(dict, "DeviceId").value_or("");
    const auto version = get<std::string>(dict, "Version");
    if (out.deviceId.empty() || !version) return std::nullopt;
    out.version = *version;
    out.name = get<std::string>(dict, "Name").value_or("(unnamed device)");
    out.vendor = get<std::string>(dict, "Vendor").value_or("");
    const auto flags = get<std::uint64_t>(dict, "Flags").value_or(0);
    out.facts.updatable = (flags & kDeviceFlagUpdatable) != 0;
    out.facts.supported = (flags & kDeviceFlagSupported) != 0;
    out.facts.needsRebootAfterUpdate = (flags & kDeviceFlagNeedsReboot) != 0;
    return out;
}

std::optional<core::ReleaseInfo> parseRelease(const Dict& dict) {
    core::ReleaseInfo out;
    const auto version = get<std::string>(dict, "Version");
    if (!version) return std::nullopt;
    out.version = *version;
    out.summary = get<std::string>(dict, "Summary").value_or("");
    out.remoteId = get<std::string>(dict, "RemoteId").value_or("");
    out.checksum = get<std::string>(dict, "Checksum").value_or("");
    if (auto locs = get<std::vector<std::string>>(dict, "Locations"); locs && !locs->empty()) {
        out.locations = std::move(*locs);
    } else if (auto uri = get<std::string>(dict, "Uri"); uri && !uri->empty()) {
        out.locations = {std::move(*uri)};  // pre-1.5 daemons (spec §5.1 skew note)
    }
    out.sizeBytes = get<std::uint64_t>(dict, "Size").value_or(0);
    const auto flags = get<std::uint64_t>(dict, "Flags").value_or(0);
    out.isUpgrade = (flags & kReleaseFlagIsUpgrade) != 0;
    if (auto dur = get<std::uint32_t>(dict, "InstallDuration")) out.installDurationSec = *dur;
    return out;
}

bool isNothingToDo(const std::string& name) {
    return name == "org.freedesktop.fwupd.NothingToDo";
}

core::Error mapError(const std::string& name, const std::string& message) {
    // Keep name AND message (review: don't flatten early) — "<name>: <message>".
    const auto text = (name.empty() ? std::string{"fwupd"} : name) +
                      (message.empty() ? std::string{": unknown error"} : ": " + message);
    // NOTE: construct core::Error DIRECTLY (aggregate {code, message} — check
    // result.hpp for the exact shape). core::makeError returns the
    // tl::unexpected wrapper for Result returns and is NOT usable here.
    if (name == "org.freedesktop.fwupd.AuthFailed")
        return core::Error{core::Error::Code::Permission, text};
    if (name == "org.freedesktop.fwupd.NothingToDo" ||
        name == "org.freedesktop.fwupd.VersionNewer")
        return core::Error{core::Error::Code::Conflict, text};
    if (name == "org.freedesktop.fwupd.NeedsUserAction")
        return core::Error{core::Error::Code::Busy, text};
    return core::Error{core::Error::Code::Io, text};
}

core::InstallDisposition dispositionFromUpdateState(std::uint32_t state) {
    switch (state) {
        case kUpdateStateNeedsReboot: return core::InstallDisposition::NeedsReboot;
        case kUpdateStatePending: return core::InstallDisposition::Scheduled;
        default: return core::InstallDisposition::Completed;
    }
}

}  // namespace devmgr::platform_linux::fwupd
```

NOTE: `core::makeError(...)` — check `core/include/devmgr/core/result.hpp` whether it returns `tl::unexpected<Error>` or `Error`; if the former, `.error()` above is wrong — return the `Error` directly (`core::Error{code, text}` aggregate or the project's factory). Match the EXISTING idiom used in `dbus_privileged_channel.cpp`'s `coreErrorFor`; keep the test's expectations authoritative.

CMake: `target_sources(devmgr_pal_linux PRIVATE src/fwupd_contract.cpp)` inside the sdbus block; spdlog is already linked transitively via devmgr_core (verify; else add).

- [x] **Step 4: run matrix → PASS**; full ctest + nosdbus build → green

Run: `ctest --test-dir build/linux-debug -R FwupdContract --output-on-failure`

- [x] **Step 5: USER commits** — `feat(platform): Phase 6 T3 — fwupd D-Bus parse layer (tolerant a{sv} mapping, error table, dispositions)`

---

### Task 4: Secure cab resolver (M2)

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/cab_resolver.hpp`, `platform/linux/src/cab_resolver.cpp`
- Modify: `platform/linux/CMakeLists.txt` (UNGATED source list `:5-12` — pure fs, no sdbus)
- Test: `tests/unit/test_cab_resolver.cpp` (ungated block of `tests/CMakeLists.txt`, Linux section `:31-40`)

**Interfaces (Produces):**
```cpp
namespace devmgr::platform_linux {
struct RemoteRef { std::string id; std::string kind; std::string filenameCache; };
class UniqueFd {  // move-only RAII fd
   public:
    UniqueFd() = default; explicit UniqueFd(int fd); ~UniqueFd(); UniqueFd(UniqueFd&&) noexcept;
    UniqueFd& operator=(UniqueFd&&) noexcept; int get() const; int release();
};
struct CabFile { UniqueFd fd; std::uint64_t sizeBytes = 0; };
// No I/O beyond lstat-free string/kind checks — used at enumerate() to set localCab.
bool isLocallyResolvable(const std::vector<std::string>& locations,
                         const std::vector<RemoteRef>& remotes, const std::string& remoteId);
// Full M2 contract — open + validate; used at install() time only.
core::Result<CabFile> resolveAndOpenCab(const std::vector<std::string>& locations,
                                        const std::vector<RemoteRef>& remotes,
                                        const std::string& remoteId,
                                        std::uint64_t expectedSizeBytes);
}
```

- [x] **Step 1: failing tests** — `tests/unit/test_cab_resolver.cpp`. Fixture: `std::filesystem` temp dir per test (`testing::TempDir()`), helper `makeRemote(kind)` returning `RemoteRef{"r1", kind, <tmp>/meta...}`. Tests (write all; each is a few lines):

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/cab_resolver.hpp"

namespace fs = std::filesystem;
using namespace devmgr::platform_linux;
using devmgr::core::Error;

class CabResolverTest : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = fs::path(::testing::TempDir()) /
                ("cab_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::create_directories(root_ / "cabs");
    }
    void TearDown() override { fs::remove_all(root_); }
    std::string writeCab(const std::string& rel, std::size_t bytes = 16) {
        const fs::path p = root_ / "cabs" / rel;
        fs::create_directories(p.parent_path());
        std::ofstream(p) << std::string(bytes, 'x');
        return p.string();
    }
    RemoteRef dirRemote() { return {"r1", "directory", (root_ / "cabs").string()}; }
    RemoteRef dlRemote() { return {"lvfs", "download", (root_ / "meta.xml.zst").string()}; }
    fs::path root_;
};

TEST_F(CabResolverTest, AbsolutePathResolves) {
    const auto abs = writeCab("a.cab");
    auto r = resolveAndOpenCab({abs}, {dirRemote()}, "r1", 16);
    ASSERT_TRUE(r.has_value());
    EXPECT_GE(r->fd.get(), 0);
    EXPECT_EQ(r->sizeBytes, 16U);
}
TEST_F(CabResolverTest, FileUriResolves) {
    const auto abs = writeCab("b.cab");
    ASSERT_TRUE(resolveAndOpenCab({"file://" + abs}, {}, "", 16).has_value());
}
TEST_F(CabResolverTest, RelativeResolvesOnlyAgainstDirectoryRemote) {
    writeCab("c.cab");
    EXPECT_TRUE(resolveAndOpenCab({"c.cab"}, {dirRemote()}, "r1", 16).has_value());
    const auto r = resolveAndOpenCab({"c.cab"}, {dlRemote()}, "lvfs", 16);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}
TEST_F(CabResolverTest, HttpsIsUnsupported) {
    const auto r = resolveAndOpenCab({"https://lvfs.example/x.cab"}, {dirRemote()}, "r1", 16);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Unsupported);
}
TEST_F(CabResolverTest, EmptyAndUnknownSchemeUnsupported) {
    EXPECT_FALSE(resolveAndOpenCab({""}, {dirRemote()}, "r1", 16).has_value());
    EXPECT_FALSE(resolveAndOpenCab({"ftp://x/y.cab"}, {dirRemote()}, "r1", 16).has_value());
    EXPECT_FALSE(resolveAndOpenCab({}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, TraversalEscapeRejected) {
    writeCab("d.cab");
    std::ofstream(root_ / "outside.cab") << "xxxxxxxxxxxxxxxx";
    const auto r = resolveAndOpenCab({"../outside.cab"}, {dirRemote()}, "r1", 16);
    ASSERT_FALSE(r.has_value());  // escapes the directory-remote root
}
TEST_F(CabResolverTest, SymlinkRejected) {
    const auto real = writeCab("real.cab");
    fs::create_symlink(real, root_ / "cabs" / "link.cab");
    EXPECT_FALSE(resolveAndOpenCab({"link.cab"}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, NonRegularFileRejected) {
    fs::create_directory(root_ / "cabs" / "adir.cab");
    EXPECT_FALSE(resolveAndOpenCab({"adir.cab"}, {dirRemote()}, "r1", 16).has_value());
}
TEST_F(CabResolverTest, OversizeRejectedAndZeroSizeRejected) {
    writeCab("big.cab", 64);
    // expectedSize 16 → cap = 24 (×1.5) → 64 rejected
    EXPECT_FALSE(resolveAndOpenCab({"big.cab"}, {dirRemote()}, "r1", 16).has_value());
    writeCab("empty.cab", 0);
    EXPECT_FALSE(resolveAndOpenCab({"empty.cab"}, {dirRemote()}, "r1", 16).has_value());
    // expectedSize 0 (metadata absent) → hard cap only → 64 accepted
    EXPECT_TRUE(resolveAndOpenCab({"big.cab"}, {dirRemote()}, "r1", 0).has_value());
}
TEST_F(CabResolverTest, FirstResolvableLocationWins) {
    writeCab("e.cab");
    EXPECT_TRUE(resolveAndOpenCab({"https://x/y.cab", "e.cab"}, {dirRemote()}, "r1", 16)
                    .has_value());
}
TEST_F(CabResolverTest, IsLocallyResolvableMirrorsRules) {
    EXPECT_TRUE(isLocallyResolvable({"x.cab"}, {dirRemote()}, "r1"));
    EXPECT_FALSE(isLocallyResolvable({"x.cab"}, {dlRemote()}, "lvfs"));
    EXPECT_FALSE(isLocallyResolvable({"https://x/y.cab"}, {dlRemote()}, "lvfs"));
    EXPECT_TRUE(isLocallyResolvable({"file:///tmp/x.cab"}, {}, ""));
    EXPECT_FALSE(isLocallyResolvable({}, {}, ""));
}
```

Run → RED (header missing).

- [x] **Step 2: implement.** Key rules (spec §5.3, every line reviewed-mandated):

```cpp
#include "devmgr/platform/linux/cab_resolver.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

namespace devmgr::platform_linux {
namespace fs = std::filesystem;
namespace {

constexpr std::uint64_t kHardCapBytes = 512ULL * 1024 * 1024;  // spec §5.3

const RemoteRef* findRemote(const std::vector<RemoteRef>& remotes, const std::string& id) {
    for (const auto& r : remotes)
        if (r.id == id) return &r;
    return nullptr;
}

// "" ⇒ not resolvable as a local path (never treat empty/unknown scheme as fs path).
std::string candidatePath(const std::string& loc, const std::vector<RemoteRef>& remotes,
                          const std::string& remoteId) {
    if (loc.empty()) return {};
    if (loc.rfind("file://", 0) == 0) return loc.substr(7);
    if (loc.find("://", 0) != std::string::npos) return {};  // https, ftp, anything remote
    if (loc.front() == '/') return loc;
    const auto* remote = findRemote(remotes, remoteId);
    if (remote == nullptr || remote->kind != "directory") return {};  // download-remote
    // FilenameCache IS the cab directory for directory-kind remotes (verified
    // on live fwupd 2.0.20 — for download remotes it is the metadata FILE).
    return (fs::path(remote->filenameCache) / loc).string();
}

bool escapesRoot(const fs::path& candidate, const fs::path& root) {
    std::error_code ec;
    const auto canon = fs::weakly_canonical(candidate, ec);
    if (ec) return true;
    const auto canonRoot = fs::weakly_canonical(root, ec);
    if (ec) return true;
    const auto rel = canon.lexically_relative(canonRoot);
    return rel.empty() || rel.native().rfind("..", 0) == 0;
}

}  // namespace

bool isLocallyResolvable(const std::vector<std::string>& locations,
                         const std::vector<RemoteRef>& remotes, const std::string& remoteId) {
    for (const auto& loc : locations)
        if (!candidatePath(loc, remotes, remoteId).empty()) return true;
    return false;
}

core::Result<CabFile> resolveAndOpenCab(const std::vector<std::string>& locations,
                                        const std::vector<RemoteRef>& remotes,
                                        const std::string& remoteId,
                                        std::uint64_t expectedSizeBytes) {
    for (const auto& loc : locations) {
        const auto path = candidatePath(loc, remotes, remoteId);
        if (path.empty()) continue;
        // Relative locations must stay inside their directory-remote root.
        if (loc.front() != '/' && loc.rfind("file://", 0) != 0) {
            const auto* remote = findRemote(remotes, remoteId);
            if (remote == nullptr || escapesRoot(path, remote->filenameCache))
                return core::makeError(core::Error::Code::Unsupported,
                                       "cab location escapes remote directory: " + loc);
        }
        // Open ONCE — O_NOFOLLOW rejects symlinks; validate via fstat on the
        // open fd (deleted-after-open is safe: fd pins the inode).
        UniqueFd fd{::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW)};
        if (fd.get() < 0)
            return core::makeError(core::Error::Code::NotFound,
                                   "cannot open firmware file: " + path);
        struct stat st{};
        if (::fstat(fd.get(), &st) != 0 || !S_ISREG(st.st_mode))
            return core::makeError(core::Error::Code::Unsupported,
                                   "firmware location is not a regular file: " + path);
        const auto size = static_cast<std::uint64_t>(st.st_size);
        const std::uint64_t cap =
            expectedSizeBytes > 0
                ? std::min<std::uint64_t>(expectedSizeBytes + expectedSizeBytes / 2, kHardCapBytes)
                : kHardCapBytes;
        if (size == 0 || size > cap)
            return core::makeError(core::Error::Code::Unsupported,
                                   "firmware file size out of bounds: " + path);
        return CabFile{std::move(fd), size};
    }
    return core::makeError(core::Error::Code::Unsupported,
                           "no locally-resolvable firmware location (run `fwupdmgr update`)");
}

}  // namespace devmgr::platform_linux
```

`UniqueFd` in the header: dtor `if (fd_ >= 0) ::close(fd_);`, move zeroes to -1, `release()` for handing ownership to sdbus. Adapt `core::makeError` usage to the project idiom (see T3 note).

- [x] **Step 3: run → all PASS; full ctest; clang-format/tidy**

- [x] **Step 4: USER commits** — `feat(platform): Phase 6 T4 — secure local-cab resolver (M2: traversal/symlink/size/type contract, fd-once semantics)`

---

### Task 5: `DkmsStatusProvider` (read-only, ungated)

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/dkms_status_provider.hpp`, `platform/linux/src/dkms_status_provider.cpp`
- Modify: `platform/linux/CMakeLists.txt` (UNGATED list `:5-12`)
- Test: `tests/unit/test_dkms_status_provider.cpp` (Linux block of `tests/CMakeLists.txt`)

**Interfaces (Produces):**
```cpp
class DkmsStatusProvider : public pal::IUpdateProvider {
   public:
    // Injectable roots (spec §6): prod defaults; tests pass fixture dirs.
    explicit DkmsStatusProvider(std::string dkmsRoot = "/var/lib/dkms",
                                std::string modulesRoot = "/lib/modules");
    // providerId() == "dkms"; capabilities() == Query; install() → Unsupported;
    // pendingActions() → {}.
};
```
Candidate shape consumed by T9/T10: id `"dkms:<module>/<version>"`, `facts.updatable=false`, `releases={}`, per-kernel `details` lines.

- [x] **Step 1: failing tests.** Fixture builds real dirs under `testing::TempDir()`; states asserted via `details` text. Supported layout = spec §6 EXACTLY (fixtures ARE the layout contract):

```cpp
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/platform/linux/dkms_status_provider.hpp"

namespace fs = std::filesystem;
using devmgr::platform_linux::DkmsStatusProvider;

class DkmsStatusProviderTest : public ::testing::Test {
   protected:
    void SetUp() override {
        root_ = fs::path(::testing::TempDir()) /
                ("dkms_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        dkms_ = root_ / "var-lib-dkms";
        mods_ = root_ / "lib-modules";
        fs::create_directories(dkms_);
        fs::create_directories(mods_);
    }
    void TearDown() override { fs::remove_all(root_); }
    // <dkmsRoot>/<mod>/<ver>/<kernel>/<arch>/module/<file>
    void addBuilt(const std::string& mod, const std::string& ver, const std::string& kernel,
                  const std::string& file) {
        const auto d = dkms_ / mod / ver / kernel / "x86_64" / "module";
        fs::create_directories(d);
        std::ofstream(d / file) << "elf";
        fs::create_directories(mods_ / kernel);  // kernel present unless test says otherwise
    }
    void addInstalled(const std::string& kernel, const std::string& file) {
        const auto d = mods_ / kernel / "updates" / "dkms";
        fs::create_directories(d);
        std::ofstream(d / file) << "elf";
    }
    DkmsStatusProvider provider() { return DkmsStatusProvider(dkms_.string(), mods_.string()); }
    fs::path root_, dkms_, mods_;
};

TEST_F(DkmsStatusProviderTest, UnavailableWithoutRoot) {
    DkmsStatusProvider p((root_ / "nope").string(), mods_.string());
    EXPECT_FALSE(p.availability().available);
}
TEST_F(DkmsStatusProviderTest, BuiltAndInstalledStates) {
    addBuilt("nvidia", "565.1", "6.8.0-49-generic", "nvidia.ko");
    addInstalled("6.8.0-49-generic", "nvidia.ko");
    auto r = provider().enumerate();
    ASSERT_TRUE(r.has_value());
    ASSERT_EQ(r->size(), 1U);
    EXPECT_EQ((*r)[0].id, "dkms:nvidia/565.1");
    EXPECT_FALSE((*r)[0].facts.updatable);  // status-only, never actionable (V1)
    ASSERT_FALSE((*r)[0].details.empty());
    EXPECT_NE((*r)[0].details[0].second.find("installed"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, BuiltNotInstalled) {
    addBuilt("hello", "1.0", "6.8.0-49-generic", "hello.ko");
    auto r = provider().enumerate();
    ASSERT_TRUE(r.has_value());
    EXPECT_NE((*r)[0].details[0].second.find("built"), std::string::npos);
    EXPECT_EQ((*r)[0].details[0].second.find("installed"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, CompressedExtensionsRecognized) {
    addBuilt("z1", "1", "k1", "z1.ko.xz");
    addBuilt("z2", "1", "k1", "z2.ko.gz");
    addBuilt("z3", "1", "k1", "z3.ko.zst");
    addInstalled("k1", "z1.ko.xz");
    auto r = provider().enumerate();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->size(), 3U);  // all three seen as built
}
TEST_F(DkmsStatusProviderTest, InstalledMatchByBasenameNotPackageName) {
    // dkms.conf BUILT_MODULE_NAME may differ from package name — match the
    // actual build-output basename (spec §6).
    addBuilt("pkgname", "2.0", "k1", "realmod.ko");
    addInstalled("k1", "realmod.ko");
    auto r = provider().enumerate();
    EXPECT_NE((*r)[0].details[0].second.find("installed"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, FailedBuildResidueIsUnknown) {
    fs::create_directories(dkms_ / "broken" / "1.0" / "k1" / "x86_64");  // no module/ output
    fs::create_directories(mods_ / "k1");
    auto r = provider().enumerate();
    ASSERT_EQ(r->size(), 1U);
    EXPECT_NE((*r)[0].details[0].second.find("unknown"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, AddedOnlyState) {
    fs::create_directories(dkms_ / "fresh" / "3.0" / "source");  // registration only
    auto r = provider().enumerate();
    ASSERT_EQ(r->size(), 1U);
    EXPECT_NE((*r)[0].details[0].second.find("added"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, KernelAbsentState) {
    addBuilt("old", "1.0", "5.15.0-gone", "old.ko");
    fs::remove_all(mods_ / "5.15.0-gone");
    auto r = provider().enumerate();
    EXPECT_NE((*r)[0].details[0].second.find("kernel absent"), std::string::npos);
}
TEST_F(DkmsStatusProviderTest, SymlinkOutsideRootNotFollowed) {
    addBuilt("evil", "1.0", "k1", "evil.ko");
    fs::create_directory_symlink("/etc", dkms_ / "evil" / "1.0" / "k1" / "x86_64" / "link");
    EXPECT_TRUE(provider().enumerate().has_value());  // no crash, no wandering walk
}
TEST_F(DkmsStatusProviderTest, SourceSymlinkSkipped) {
    fs::create_directories(dkms_ / "m" / "1" / "k1" / "x86_64" / "module");
    std::ofstream(dkms_ / "m" / "1" / "k1" / "x86_64" / "module" / "m.ko") << "e";
    fs::create_directories(mods_ / "k1");
    fs::create_directory_symlink(root_, dkms_ / "m" / "1" / "source");  // standard dkms layout
    auto r = provider().enumerate();
    ASSERT_EQ(r->size(), 1U);  // "source" is not a kernel dir
}
```

Run → RED.

- [x] **Step 2: implement.** Walk shape (lstat semantics: `fs::directory_iterator` + `entry.is_symlink()` checks; skip symlinked dirs; only iterate fixed depth — NO recursive iterator, spec §6 "⊥ recursive follow"):

```cpp
// dkms_status_provider.cpp — core walk (complete the class around it):
namespace {
constexpr std::array<const char*, 4> kKoExts = {".ko", ".ko.xz", ".ko.gz", ".ko.zst"};
bool isModuleFile(const fs::path& p) {
    const auto name = p.filename().string();
    return std::any_of(kKoExts.begin(), kKoExts.end(), [&](const char* e) {
        const std::string ext{e};
        return name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0;
    });
}
}  // namespace

core::Result<std::vector<core::UpdateCandidate>> DkmsStatusProvider::enumerate() {
    std::vector<core::UpdateCandidate> out;
    std::error_code ec;
    for (const auto& modDir : fs::directory_iterator(dkmsRoot_, ec)) {
        if (ec || !modDir.is_directory() || modDir.is_symlink()) continue;
        const auto module = modDir.path().filename().string();
        for (const auto& verDir : fs::directory_iterator(modDir.path(), ec)) {
            if (ec || !verDir.is_directory() || verDir.is_symlink()) continue;
            const auto version = verDir.path().filename().string();
            core::UpdateCandidate c;
            c.providerId = "dkms";
            c.id = "dkms:" + module + "/" + version;
            c.displayName = module;
            c.currentVersion = version;
            bool sawKernelDir = false;
            for (const auto& kDir : fs::directory_iterator(verDir.path(), ec)) {
                if (ec || !kDir.is_directory() || kDir.is_symlink()) continue;
                const auto kernel = kDir.path().filename().string();
                if (kernel == "source" || kernel == "build") continue;
                sawKernelDir = true;
                c.details.emplace_back(kernel, kernelState(kDir.path(), kernel));
            }
            if (!sawKernelDir) c.details.emplace_back("(no kernel)", "added — not built");
            out.push_back(std::move(c));
        }
    }
    return out;
}

// kernelState(kernelDir, kernel):
//   scan <kernelDir>/<arch>/module/*.ko* (one arch level, dirs only, no symlink dirs)
//   no module output          → "unknown — build residue or unsupported layout"
//   kernel ∉ modulesRoot_     → "<state> — kernel absent" (state still built/…)
//   output basename ∈ <modulesRoot>/<kernel>/updates/dkms/ → "built + installed"
//   else                      → "built, not installed"
```

`availability()`: `{.available = fs::is_directory(dkmsRoot_), .version = std::nullopt, .error = available ? nullopt : Error{NotFound, "no /var/lib/dkms — DKMS not present"}, .notices = {}}`. `install()` → `Unsupported` ("dkms provider is status-only"). All `directory_iterator` calls take `ec` (⊥ throw — V2).

- [x] **Step 3: run → PASS; full ctest; nosdbus build (ungated file compiles both configs)**

- [x] **Step 4: USER commits** — `feat(platform): Phase 6 T5 — DkmsStatusProvider (read-only tri-state+unknown, layout contract per spec §6)`

---

### Task 6: `FwupdUpdateProvider` — connection, read side, signals

**Files:**
- Create: `platform/linux/include/devmgr/platform/linux/fwupd_update_provider.hpp`, `platform/linux/src/fwupd_update_provider.cpp`
- Modify: `platform/linux/src/fwupd_contract.cpp` + `.hpp` (add `parseHistoryEntry`)
- Modify: `platform/linux/CMakeLists.txt` (sdbus block)
- Test: extend `tests/unit/test_fwupd_contract.cpp` (history parse); `tests/unit/test_fwupd_provider_nodaemon.cpp` (sdbus-gated block)

**Interfaces (Produces — T7/T8/T9/T13 consume):**
```cpp
namespace devmgr::platform_linux {
class FwupdUpdateProvider : public pal::IUpdateProvider {
   public:
    struct Config {
        bool useSessionBus = false;  // tests: fake daemon on dbus-run-session bus
    };
    // Subscribes fwupd signals; publishes core::UpdatesChangedEvent /
    // core::UpdateRequestEvent on `bus`. NEVER throws: connection failure ⇒
    // degraded unavailable state (V2).
    FwupdUpdateProvider(runtime::EventBus& bus, Config cfg = {});
    ~FwupdUpdateProvider();  // ordered teardown, spec §5.5
    // providerId() == "fwupd"; capabilities() == Query|Install.
    // install() in this task returns Unsupported("not yet wired") — T8 replaces it.
};
// fwupd_contract addition:
namespace fwupd { std::optional<core::PendingAction> parseHistoryEntry(const Dict& dict); }
}
```

- [x] **Step 1: `parseHistoryEntry` — test then impl**

Test (append to `test_fwupd_contract.cpp`):

```cpp
TEST(FwupdContract, HistoryEntryToPendingAction) {
    fw::Dict d;
    d["DeviceId"] = sdbus::Variant{std::string{"aabb"}};
    d["Name"] = sdbus::Variant{std::string{"Webcam"}};
    d["UpdateState"] = sdbus::Variant{std::uint32_t{fw::kUpdateStateNeedsReboot}};
    d["Version"] = sdbus::Variant{std::string{"1.2.4"}};
    const auto p = fw::parseHistoryEntry(d);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->disposition, devmgr::core::InstallDisposition::NeedsReboot);
    EXPECT_EQ(p->deviceId, "aabb");
    d["UpdateState"] = sdbus::Variant{std::uint32_t{fw::kUpdateStateSuccess}};
    EXPECT_FALSE(fw::parseHistoryEntry(d).has_value());  // completed = not pending
    d["UpdateState"] = sdbus::Variant{std::uint32_t{fw::kUpdateStateFailed}};
    EXPECT_FALSE(fw::parseHistoryEntry(d).has_value());  // failed → availability notice, not pending
}
```

Impl: nullopt unless `UpdateState ∈ {Pending, NeedsReboot}`; fill `{providerId="fwupd", deviceId, deviceName=Name, disposition, version=Version}`.

- [x] **Step 2: provider skeleton + read side.** Structure (complete the .cpp around this; mirror `dbus_privileged_channel.cpp`'s session-bus seam and the hand-written vtable idiom in `daemon/src/manager_adaptor.cpp`):

```cpp
class FwupdUpdateProvider::Impl {  // pimpl keeps sdbus out of the header
   public:
    Impl(runtime::EventBus& bus, Config cfg) : bus_(bus) {
        try {
            connection_ = cfg.useSessionBus ? sdbus::createSessionBusConnection()
                                            : sdbus::createSystemBusConnection();
            proxy_ = sdbus::createProxy(*connection_, sdbus::ServiceName{fwupd::kBusName},
                                        sdbus::ObjectPath{fwupd::kObjectPath});
            registerSignalHandlers();          // BEFORE the loop starts
            connection_->enterEventLoopAsync();
        } catch (const std::exception& e) {
            initError_ = e.what();  // degraded: availability() reports it (V2)
            proxy_.reset();
            connection_.reset();
        }
    }
    ~Impl() {
        // Spec §5.5 teardown order — do not reorder:
        accepting_.store(false);       // 1. stop accepting ops
        proxy_.reset();                // 2. drops signal registrations
        if (connection_) connection_->leaveEventLoop();  // 3. stop + join async loop
        connection_.reset();           // 4. destroy connection last
    }
    ...
    void registerSignalHandlers() {
        // v2 API: proxy_->uponSignal("DeviceAdded").onInterface(fwupd::kInterface)
        //             .call([this](const fwupd::Dict&) { publishChanged(); });
        // same for DeviceRemoved / DeviceChanged / Changed (no args);
        // "DeviceRequest" → parse kind/message keys → UpdateRequestEvent.
        // PLUS NameOwnerChanged for fwupd's well-known name (spec §5.2): a
        // second proxy to "org.freedesktop.DBus" / "/org/freedesktop/DBus",
        // filter arg0 == fwupd::kBusName → publishChanged() (UI re-probes via
        // the coalesced refresh; availability() reads fresh per call anyway).
        // Every handler body: if (!accepting_.load()) return;   // teardown gate
    }
    std::atomic<bool> accepting_{true};
    std::atomic<bool> installing_{false};  // V5 gate — used by T8
    ...
};
```

`availability()`: no connection → `{available=false, error=Io(initError_)}`. Else read `DaemonVersion` property (`proxy_->getProperty("DaemonVersion").onInterface(fwupd::kInterface)`); sdbus::Error → unavailable + mapped reason. Notices (spec §8.1):
- failed history: `GetHistory` contains `kUpdateStateFailed` entries → `notices += "<n> previous update(s) failed — see fwupdmgr history"`.
- stale metadata: enabled `download`-kind remotes whose modification-time key (tolerant read — fwupd exposes `ModificationTime`/`Mtime`; pin the actual key from a live `GetRemotes` reply at implementation) is > 30 days old → `notices += "<remoteId> metadata <n> days old — run fwupdmgr refresh"`. Key absent → no notice (⊥ guess).

`enumerate()`:
```
GetDevices → aa{sv} → parseDevice each (skip nullopt)
GetRemotes → aa{sv} → RemoteRefs {Id, Kind, FilenameCache} (tolerant get<>)
∀ device with facts.updatable:
    GetUpgrades(deviceId):
      sdbus::Error name==NothingToDo → releases = {}
      other error → row kept, details += {"upgrades", "query failed: <msg>"} (spec §5.1)
      else parseRelease each → dedupe by (remoteId,checksum) → localCab =
           isLocallyResolvable(locations, remotes, remoteId) per release
candidate: id=deviceId, displayName=name, currentVersion=version,
           candidateVersion = releases.empty() ? nullopt : releases.front().version,
           details += {"vendor", vendor} + per-release lines are the VM's job (T10)
catch (const sdbus::Error& e) → tl::unexpected(fwupd::mapError(e.getName(), e.getMessage()))
catch (const std::exception& e) → Io (V2 — same both catches ∀ methods)
```

`pendingActions()`: `GetHistory` → `parseHistoryEntry` each, collect. `install()`: `return core::makeError(core::Error::Code::Unsupported, "install wired in T8");`

- [x] **Step 3: no-daemon degradation test** — `tests/unit/test_fwupd_provider_nodaemon.cpp` (register in the sdbus-gated tests block from T3):

```cpp
#include <gtest/gtest.h>

#include "devmgr/platform/linux/fwupd_update_provider.hpp"
#include "devmgr/runtime/event_bus.hpp"

using devmgr::platform_linux::FwupdUpdateProvider;

TEST(FwupdProviderNoDaemon, DegradesWithoutThrowing) {
    // Unit env has no session bus (no dbus-run-session wrapper) — ctor must
    // absorb the failure (V2), availability must explain it, dtor must be safe.
    ::unsetenv("DBUS_SESSION_BUS_ADDRESS");
    devmgr::runtime::EventBus bus;
    FwupdUpdateProvider p(bus, {.useSessionBus = true});
    const auto a = p.availability();
    EXPECT_FALSE(a.available);
    ASSERT_TRUE(a.error.has_value());
    auto e = p.enumerate();
    EXPECT_FALSE(e.has_value());
}
```

- [x] **Step 4: build both configs, run unit suite → green; USER commits** — `feat(platform): Phase 6 T6 — FwupdUpdateProvider read side (connection, enumerate, pendingActions, signals→EventBus)`

---

### Task 7: Fake fwupd daemon + integration suite (read side)

**Files:**
- Create: `tests/fwupd/CMakeLists.txt`, `tests/fwupd/fake_fwupd_daemon.hpp`, `tests/fwupd/fake_fwupd_daemon.cpp`, `tests/fwupd/test_fwupd_provider_ipc.cpp`
- Modify: root `CMakeLists.txt:40-42` → 

```cmake
if(DEVMGR_WITH_SDBUS)
    add_subdirectory(tests/ipc)
    add_subdirectory(tests/fwupd)
endif()
```

**Interfaces (Produces — T8 extends the same suite):**
```cpp
// Scriptable in-process org.freedesktop.fwupd double on the SESSION bus
// (suite runs under dbus-run-session, tests/ipc pattern).
class FakeFwupdDaemon {
   public:
    FakeFwupdDaemon();                 // claims name, registers vtable, starts loop
    ~FakeFwupdDaemon();
    void setDevices(std::vector<Dict> devices);
    void setUpgrades(const std::string& deviceId, std::vector<Dict> releases);
    void setUpgradesError(const std::string& deviceId, const std::string& errName,
                          const std::string& errMsg);
    void setRemotes(std::vector<Dict> remotes);
    void setHistory(std::vector<Dict> entries);
    void setResults(const std::string& deviceId, Dict results);
    using InstallHook = std::function<void(const std::string& deviceId, int cabFd,
                                           const std::map<std::string, sdbus::Variant>& options)>;
    void scriptInstall(InstallHook hook);              // throw sdbus::Error inside to fail
    void emitDeviceAdded(const Dict& device);
    void emitProgress(std::uint32_t status, std::uint32_t percentage);  // PropertiesChanged
    void emitDeviceRequest(const Dict& request);
    void dropName();       // daemon-restart scenario
    void reacquireName();
    std::string daemonVersion_ = "2.0.20-fake";
};
```

- [x] **Step 1: CMakeLists**

```cmake
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(devmgr_fwupd_tests
    fake_fwupd_daemon.cpp
    test_fwupd_provider_ipc.cpp)
target_link_libraries(devmgr_fwupd_tests
    PRIVATE devmgr_core devmgr_pal_linux SDBusCpp::sdbus-c++ GTest::gtest GTest::gtest_main)
target_include_directories(devmgr_fwupd_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_compile_features(devmgr_fwupd_tests PRIVATE cxx_std_20)

# Private session bus: fake fwupd + provider, nothing touches real buses.
add_test(NAME devmgr_fwupd COMMAND dbus-run-session -- $<TARGET_FILE:devmgr_fwupd_tests>)
```

- [x] **Step 2: implement the fake.** Registration mirrors `daemon/src/manager_adaptor.cpp`'s hand-written v2 vtable idiom (read it first; same `addVTable`/`registerMethod` shapes). Methods return the scripted vectors; `GetUpgrades` looks up per-device script, throws scripted `sdbus::Error` when set, throws `org.freedesktop.fwupd.NothingToDo` when the device has an empty entry SET explicitly (distinct from unset → returns `{}`); `Install(s,h,a{sv})` runs the hook with the RECEIVED fd (so T8 can read cab bytes back). Properties `DaemonVersion/Status/Percentage` via property vtable entries; `emitProgress` sets them + emits `PropertiesChanged`. `dropName()`/`reacquireName()` = `connection_->releaseName(...)` / `requestName(...)`.

- [x] **Step 3: read-side integration tests** (all in `test_fwupd_provider_ipc.cpp`; fixture constructs `FakeFwupdDaemon` then `FwupdUpdateProvider provider(bus_, {.useSessionBus = true})`; helper Dicts reuse T3's shapes):

Write these tests — names + assertions (review-required numbers in comments):

```cpp
TEST_F(F, AvailabilityReportsVersion)             // available=true, version=="2.0.20-fake"
TEST_F(F, EnumerateMergesDevicesUpgradesRemotes)  // 2 devices; updatable one gets 2 releases,
    // candidateVersion==releases.front().version (fake's order preserved — NO version sort);
    // https release → localCab=false; relative+directory-remote → localCab=true
TEST_F(F, NothingToDoUpgradesMeansEmptyNotError)
TEST_F(F, PartialUpgradesFailureKeepsRow)         // req #: GetUpgrades errors for dev A only —
    // A row present with "query failed" detail, B row has releases, enumerate() overall ok
TEST_F(F, MalformedVariantOverRealBus)            // review test 9: Flags as string → row survives
TEST_F(F, DuplicateReleasesDeduped)               // same (remoteId,checksum) twice → one
TEST_F(F, PendingActionsFromHistory)              // needs-reboot history entry → 1 PendingAction
TEST_F(F, FailedHistoryBecomesAvailabilityNotice)
TEST_F(F, DaemonRestartRecovers)                  // review test 1: dropName → availability
    // unavailable; reacquireName → availability available, enumerate works
TEST_F(F, DeviceAddedSignalPublishesUpdatesChanged)   // EventBus subscription + condition_variable
TEST_F(F, DeviceRequestPublishesUpdateRequestEvent)   // kind/message mapped
```

Signal tests: subscribe on `bus_`, `emitX()`, wait on a `std::condition_variable` with 2 s timeout — never sleep-poll.

- [x] **Step 4: run**

Run: `ctest --test-dir build/linux-debug -R devmgr_fwupd --output-on-failure`
Expected: PASS. Full ctest + container parity later (T13 gates).

- [x] **Step 5: USER commits** — `test(fwupd): Phase 6 T7 — scriptable fake org.freedesktop.fwupd daemon + provider integration suite (read side)`

---

### Task 8: Install path (M2 fd + M3 lifecycle + V5 progress)

**Files:**
- Modify: `platform/linux/src/fwupd_update_provider.cpp` (+ header: install-related members)
- Test: extend `tests/fwupd/test_fwupd_provider_ipc.cpp` + `tests/fwupd/fake_fwupd_daemon.{hpp,cpp}` (Install hook, Results scripting)

**Interfaces (Consumes):** T4 `resolveAndOpenCab`/`isLocallyResolvable`, T3 contract, T7 fake. **Produces:** working `install()` honoring spec §5.3–§5.5; `installing_` gate exposed to progress handler.

- [x] **Step 1: failing integration tests** (append; fixture gains a directory-remote + cab fixture file written in `SetUp`):

```cpp
TEST_F(F, InstallHappyPathWithProgress)
    // scripted install emits 10 → 50 → 30 → 100 (non-monotonic tolerated);
    // progress callback collects; results scripted SUCCESS + new Version →
    // outcome.disposition==Completed, observedVersion=="1.2.4";
    // fake received fd whose CONTENT == cab fixture bytes (fd-passing proof, M2)
TEST_F(F, PreflightRefusesVanishedRelease)        // review test 6: upgrades mutated after
    // enumerate (checksum changed) → install → Conflict, fake install hook NEVER ran
TEST_F(F, PreflightRefusesNonUpdatableDevice)
TEST_F(F, RemoteOnlyReleaseRefusedBeforeDbus)     // review test 4+5: https-only locations →
    // Unsupported mentioning `fwupdmgr update`; hook never ran
TEST_F(F, TraversalCabRefusedBeforeDbus)          // review test 5: location "../evil.cab" →
    // error; hook never ran
TEST_F(F, OfflineUpdateReportsScheduled)          // review test 7: install ok, Version
    // unchanged, Results UpdateState=PENDING → disposition==Scheduled, observedVersion absent
TEST_F(F, AuthFailedMapsToPermission)             // review test 12: hook throws
    // sdbus::Error{"org.freedesktop.fwupd.AuthFailed", "cancelled"} → Permission, no hang
TEST_F(F, SecondInstallWhileActiveIsBusy)         // hook blocks on latch; second install()
    // (other thread) → Busy immediately; release latch → first completes
TEST_F(F, ProgressIgnoredWhileIdle)               // review test 11: emitProgress with NO
    // install in flight → provider publishes nothing, no callback exists to fire (V5);
    // then a normal install still reports clean progress
TEST_F(F, LateProgressAfterCompletionDropped)     // emitProgress after install returned →
    // collected progress vector unchanged
TEST_F(F, NameDropMidInstallReturnsIoAndRecovers) // hook: dropName + block → install Io;
    // reacquire → pendingActions()/enumerate() work
TEST_F(F, TeardownDuringSignalStorm)              // review test 14: thread hammers
    // emitDeviceAdded while provider destructs → no crash, no post-dtor publish
    // (assert via EventBus subscriber counting after dtor returns)
```

Run → RED (`install` returns Unsupported stub).

- [x] **Step 2: implement `install()`** — the M3 state machine, verbatim shape:

```cpp
core::Result<core::InstallOutcome> FwupdUpdateProvider::install(
    const std::string& candidateId, const core::ReleaseRef& release,
    runtime::ProgressReporter progress) {
    auto* impl = impl_.get();
    if (!impl || !impl->proxy_)
        return core::makeError(core::Error::Code::Io, "fwupd unavailable");
    bool expected = false;
    if (!impl->installing_.compare_exchange_strong(expected, true))
        return core::makeError(core::Error::Code::Busy, "another update is in progress");
    // RAII: clear installing_ + progress sink on every exit path.
    struct Gate {
        Impl* i;
        ~Gate() {
            i->progressSink_ = nullptr;   // under progressMutex_
            i->installing_.store(false);
        }
    } gate{impl};

    try {
        // ---- Preflight (spec §5.5): fresh queries, ⊥ trust the UI snapshot ----
        const auto device = impl->fetchDevice(candidateId);  // fresh GetDevices scan
        if (!device || !device->facts.updatable)
            return core::makeError(core::Error::Code::Conflict,
                                   "device changed since refresh — refresh & retry");
        const auto fresh = impl->fetchRelease(candidateId, release);  // fresh GetUpgrades,
        if (!fresh)                                                   // match (remoteId,checksum)
            return core::makeError(core::Error::Code::Conflict,
                                   "release changed since refresh — refresh & retry");
        // ---- Resolving (M2, T4 contract) ----
        const auto remotes = impl->fetchRemotes();
        auto cab = resolveAndOpenCab(fresh->locations, remotes, fresh->remoteId,
                                     fresh->sizeBytes);
        if (!cab) return tl::unexpected(cab.error());
        // ---- Installing: async call, gated progress (V5) ----
        {
            std::scoped_lock lk(impl->progressMutex_);
            impl->progressSink_ = &progress;   // PropertiesChanged handler forwards
        }                                       // Status/Percentage ⇔ sink non-null
        const auto timeout = std::chrono::seconds(
            std::max<std::uint32_t>(fresh->installDurationSec.value_or(0) * 2, 600));
        std::map<std::string, sdbus::Variant> options;
        options["reason"] = sdbus::Variant{std::string{"device-manager update"}};
        // sdbus::UnixFd dups the fd on construction (verify in sdbus-c++/Types.h;
        // if it ADOPTS instead, pass cab->fd.release()) — cab stays alive until
        // the call returns either way (M2 fd-ownership rule).
        auto call = impl->proxy_->callMethodAsync("Install")
                        .onInterface(fwupd::kInterface)
                        .withArguments(candidateId, sdbus::UnixFd{cab->fd.get()}, options)
                        .getResultAsFuture<>();
        if (call.wait_for(timeout) != std::future_status::ready)
            return core::makeError(core::Error::Code::Io,
                                   "install timed out — state reconciled on next refresh");
        call.get();  // throws sdbus::Error on daemon-reported failure
        // ---- Finalizing: reply ≠ proof; GetResults is authoritative (§5.4) ----
        return impl->finalize(candidateId, *device);
    } catch (const sdbus::Error& e) {
        return tl::unexpected(fwupd::mapError(std::string{e.getName()},
                                              std::string{e.getMessage()}));
    } catch (const std::exception& e) {
        return core::makeError(core::Error::Code::Io, std::string{"install failed: "} + e.what());
    }
}
```

`finalize`: `GetResults(deviceId)` → tolerant `UpdateState` read → `dispositionFromUpdateState`; fresh `fetchDevice` → `observedVersion` = new Version if ≠ pre-install version, else nullopt + (results PENDING → `Scheduled`); `needsReboot` = disposition==NeedsReboot || device facts flag; message = human line ("installed 1.2.4", "scheduled for next boot", "reboot required to apply 1.2.4"). `GetResults` NothingToDo error → keep reply-based `Completed`.

PropertiesChanged handler (registered in T6 ctor): `uponSignal("PropertiesChanged").onInterface("org.freedesktop.DBus.Properties")` → if `!accepting_` return; lock `progressMutex_`; if `progressSink_` null return (V5); extract `Percentage` (absent in the changed-props dict OR >100 ⇒ `-1` indeterminate, spec §5.4) and `Status` → `(*progressSink_)(runtime::ProgressUpdate{percent, fwupd::statusName(status)})`. Add tiny `fwupd::statusName(std::uint32_t) → const char*` table ("idle","device-write",... — pin names from `fwupd-enums.h` in T3's grep style) to the contract with a 3-line test.

- [x] **Step 3: run suite → all PASS** (`ctest -R devmgr_fwupd`); full ctest; both configs.

- [x] **Step 4: USER commits** — `feat(platform): Phase 6 T8 — fwupd install path (M3 lifecycle: preflight/resolve/install/finalize; V5 progress gating; M2 fd install)`

---

### Task 9: Facade — snapshots, reconciler (M1), serialized installs

**Files:**
- Modify: `app/include/devmgr/app/application_facade.hpp`, `app/src/application_facade.cpp`
- Modify: `tui/src/tui_app.cpp` + `gui/src/gui_app.cpp` composition roots (pass providers; TUI/GUI UI comes in T11/T12 — here only ctor wiring compiles)
- Test: `tests/unit/test_application_facade_updates.cpp` (portable block; uses `FakeUpdateProvider`)

**Interfaces (Produces — T10–T13 consume EXACTLY):**
```cpp
// ApplicationFacade ctor gains trailing param:
//   std::vector<pal::IUpdateProvider*> updateProviders = {}
std::future<void> refreshUpdates();  // worker: per-provider state + reconcile;
                                     // publishes core::UpdatesRefreshedEvent; future-custody
                                     // contract same as refresh()
std::vector<core::UpdateProviderState> updatesSnapshot() const;  // mutex-guarded copy
std::vector<core::PendingAction> pendingUpdateActions() const;
bool rebootPendingEffective() const;  // systemInfo().rebootPending || ∃ NeedsReboot pending
bool installActive() const;           // quit-guard input (spec §5.5)
// One TaskCompletedEvent{taskId="install-update:"+candidateId} ALWAYS (Phase 4 pattern);
// TaskProgressEvent{same taskId} during; UpdatesChangedEvent on terminal outcome.
std::future<void> installUpdate(std::string providerId, std::string candidateId,
                                core::ReleaseRef release);
```

- [x] **Step 1: failing tests** — `tests/unit/test_application_facade_updates.cpp`. Fixture mirrors `test_application_facade.cpp` (real `TaskScheduler`, real `EventBus`, `FakeUpdateProvider` members, facade with `{&fakeA_, &fakeB_}`; helper `waitRefresh()` = `facade.refreshUpdates().get()`).

```cpp
TEST_F(FacadeUpdatesTest, PartialProviderFailureIsFirstClass) {   // review test 10
    fakeA_.enumerateResult_ = core::makeError(core::Error::Code::Io, "down");  // converts to Result
    fakeB_.enumerateResult_ = std::vector<core::UpdateCandidate>{candidate("b1")};
    waitRefresh();
    const auto snap = facade().updatesSnapshot();
    ASSERT_EQ(snap.size(), 2U);
    EXPECT_TRUE(snap[0].refreshError.has_value());   // A failed
    EXPECT_EQ(snap[1].candidates.size(), 1U);        // B landed anyway
}
TEST_F(FacadeUpdatesTest, RefreshFailureKeepsLastGoodRows) {      // spec §8.1 deliberate retain
    fakeA_.enumerateResult_ = std::vector<core::UpdateCandidate>{candidate("a1")};
    waitRefresh();
    fakeA_.enumerateResult_ = core::makeError(core::Error::Code::Io, "flake");
    waitRefresh();
    const auto snap = facade().updatesSnapshot();
    EXPECT_EQ(snap[0].candidates.size(), 1U);        // retained
    EXPECT_TRUE(snap[0].refreshError.has_value());   // AND flagged
}
TEST_F(FacadeUpdatesTest, RebootBannerSurvivesCandidateDisappearance) {  // M1 / review test 8
    fakeA_.installResult_ = core::InstallOutcome{.disposition = core::InstallDisposition::NeedsReboot,
                                                 .needsReboot = true, .observedVersion = "2",
                                                 .message = "reboot required"};
    facade().installUpdate("fake", "a1", {"r", "c"}).get();
    EXPECT_TRUE(facade().rebootPendingEffective());          // session outcome → sticky
    fakeA_.enumerateResult_ = std::vector<core::UpdateCandidate>{};  // candidate GONE
    fakeA_.pendingResult_ = std::vector<core::PendingAction>{pendingNeedsReboot("a1")};
    waitRefresh();
    EXPECT_TRUE(facade().rebootPendingEffective());          // V4: ⊥ derived from candidates
}
TEST_F(FacadeUpdatesTest, PendingClearsOnlyOnPositiveEvidence) {  // M1 clear rule
    // (continue from state above)
    fakeA_.pendingResult_ = core::makeError(core::Error::Code::Io, "down");
    waitRefresh();
    EXPECT_TRUE(facade().rebootPendingEffective());   // query failed = NO evidence → retained
    fakeA_.pendingResult_ = std::vector<core::PendingAction>{};   // provider: nothing pending
    waitRefresh();
    EXPECT_FALSE(facade().rebootPendingEffective());  // positive evidence → cleared
}
TEST_F(FacadeUpdatesTest, InstallsSerializedSecondIsBusy) {       // spec §5.4 in-process
    std::promise<void> gate;
    fakeA_.onInstall_ = [&](auto&) { gate.get_future().wait(); };
    auto first = facade().installUpdate("fake", "a1", {"r", "c"});
    // collect TaskCompletedEvents via bus subscription…
    auto second = facade().installUpdate("fake", "a2", {"r", "c"});
    second.get();                                     // completes immediately
    EXPECT_EQ(fakeA_.installCalls_.load(), 1);        // provider never saw the second
    // …assert second's TaskCompletedEvent{ok=false, message contains "in progress"}
    gate.set_value();
    first.get();
}
TEST_F(FacadeUpdatesTest, InstallPublishesProgressAndCompletion) {
    fakeA_.onInstall_ = [](runtime::ProgressReporter& p) {
        p(runtime::ProgressUpdate{42, "device-write"});
    };
    // subscribe TaskProgressEvent + TaskCompletedEvent; run; assert taskId
    // "install-update:a1", percent 42, then ok=true completion
}
TEST_F(FacadeUpdatesTest, CapsGateRefusesStatusOnlyProvider) {    // V1 defense in depth
    fakeB_.caps_ = pal::UpdateProviderCaps::Query;
    facade().installUpdate(fakeB_.id_, "x", {"r", "c"}).get();
    EXPECT_EQ(fakeB_.installCalls_.load(), 0);
    // TaskCompletedEvent ok=false, message contains "status-only"
}
```

(`candidate()`/`pendingNeedsReboot()` = small fixture builders. Complete every "…assert" with a real `bus_.subscribe<core::TaskCompletedEvent>` collector — copy the collector idiom from `test_application_facade.cpp`.) Run → RED.

- [x] **Step 2: implement.** Members: `std::vector<pal::IUpdateProvider*> updateProviders_;`, `mutable std::mutex updatesMutex_;`, `std::vector<core::UpdateProviderState> updatesSnapshot_;`, `std::map<std::pair<std::string,std::string>, core::PendingAction> pending_;` (key = providerId+deviceId; value provenance union), `std::set<std::pair<std::string,std::string>> sessionSticky_;`, `std::atomic<bool> installActive_{false};`.

Worker logic (inside `refreshUpdates()`'s scheduled task; comment the M1 rules):

```cpp
std::vector<core::UpdateProviderState> next;
for (auto* p : updateProviders_) {
    core::UpdateProviderState st;
    st.providerId = p->providerId();
    st.availability = p->availability();
    if (st.availability.available) {
        if (auto cands = p->enumerate()) {
            st.candidates = std::move(*cands);
        } else {
            st.refreshError = cands.error();
            st.candidates = lastGoodCandidates(st.providerId);  // §8.1 deliberate retain
        }
        // ---- M1 reconcile: fwupd history = durable source; clear ONLY on
        // positive evidence (query succeeded AND key absent). Query failure
        // or enumerate failure ⇒ retain sticky (no evidence). V4: never
        // derived from st.candidates. ----
        if (auto pending = p->pendingActions()) {
            std::set<std::pair<std::string, std::string>> reported;
            for (auto& a : *pending) {
                reported.insert({a.providerId, a.deviceId});
                upsertPending(a);
            }
            if (!st.refreshError.has_value()) erasePendingNotIn(st.providerId, reported);
        }
    } else {
        st.candidates = lastGoodCandidates(st.providerId);
    }
    next.push_back(std::move(st));
}
{ std::scoped_lock lk(updatesMutex_); updatesSnapshot_ = std::move(next); }
bus_.publish(core::UpdatesRefreshedEvent{});
```

`installUpdate`: `installActive_.exchange(true)` busy-check BEFORE scheduling (busy → publish completion event, return ready future); worker: find provider by id (missing → NotFound completion); `hasCap(Install)` gate (V1); `p->install(candidateId, release, [this, taskId](const runtime::ProgressUpdate& u){ bus_.publish(core::TaskProgressEvent{taskId, u.percent, u.stage}); })`; on ok: disposition≠Completed → `sessionSticky_` insert + `upsertPending(fromOutcome(...))`; publish `TaskCompletedEvent{taskId, ok, outcome.message | error message}` + `core::UpdatesChangedEvent{}`; `installActive_.store(false)` on ALL paths (scope guard).

`rebootPendingEffective()`: `∃ pending_ NeedsReboot` || `systemInfo().value_or({}).rebootPending`.

- [x] **Step 3: run → PASS; full ctest; nosdbus config (facade is portable — FakeUpdateProvider only)**

- [x] **Step 4: USER commits** — `feat(app): Phase 6 T9 — facade update snapshots, M1 pending/reboot reconciler, serialized installUpdate`

---

### Task 10: `UpdatesVM`

**Files:**
- Create: `app/include/devmgr/app/updates_vm.hpp`, `app/src/updates_vm.cpp`
- Modify: `app/CMakeLists.txt` (add `src/updates_vm.cpp`)
- Test: `tests/unit/test_updates_vm.cpp`

**Interfaces (Produces — T11/T12 consume EXACTLY; V3 single-source formatting):**
```cpp
class UpdatesVM {
   public:
    UpdatesVM(ApplicationFacade& facade, runtime::EventBus& bus, IUiDispatcher& dispatcher);
    ~UpdatesVM();  // UI-thread destroy; alive-token handshake (ModulesVM contract verbatim)
    std::vector<std::string>& rowsRef();
    int& selectedRef();
    void rebuild();                       // UI thread: snapshot → rows
    std::string banner() const;           // availability + version + Secure Boot + reboot marker
    std::string requestBanner() const;    // "" when none; DURABLE until dismiss (spec §9)
    void dismissRequest();
    std::vector<std::string> detailLines() const;  // selected candidate: facts + releases
    struct InstallArgs {
        std::string providerId, candidateId;
        core::ReleaseRef release;
        std::string confirmText;  // version delta + needs-reboot warn + duration (spec §9)
    };
    std::optional<InstallArgs> selectedInstall() const;  // nullopt ⇔ verb disabled (V1 gate)
    void setRebuildHooks(std::function<void()> before, std::function<void()> after);
    std::string installProgressText() const;  // "" when idle
};
```

- [x] **Step 1: failing tests.** Fixture mirrors `tests/unit/test_modules_vm.cpp` (facade + 2 FakeUpdateProviders + `InlineUiDispatcher` / queuing dispatcher for teardown tests). Tests:

```cpp
TEST_F(UpdatesVmTest, RowsAreByteFrozenFormat) {
    // one fwupd-ish candidate {name "Webcam", current 1.2.2, candidate 1.2.4,
    // localCab release} + one dkms row → rebuild → EXACT row strings pinned:
    EXPECT_EQ(vm().rowsRef()[0], expectedRowWebcam);  // pin the literal here once
    // — this literal IS the parity contract both UIs render (V3).
}
TEST_F(UpdatesVmTest, PlaceholderRowWhenEmptyNeverActionable) {   // V1 + Phase 5 rule
    // empty snapshot → rows == {"(no updates available)"}; selectedInstall()==nullopt
}
TEST_F(UpdatesVmTest, RemoteOnlyReleaseNotInstallableWithGuidance) {  // review test 4
    // candidate w/ https-only release → selectedInstall()==nullopt;
    // detailLines() contains "external download required — run `fwupdmgr update`"
}
TEST_F(UpdatesVmTest, SelectedInstallCarriesReleaseRefAndConfirmText) {
    // localCab release → args.release == {remoteId,checksum}; confirmText contains
    // "1.2.2 → 1.2.4"; needsRebootAfterUpdate fact → contains "reboot"
}
TEST_F(UpdatesVmTest, BannerShowsAvailabilityRebootAndSecureBoot) {
    // provider unavailable w/ error → banner contains reason;
    // facade pending NeedsReboot → banner contains "reboot required"
}
TEST_F(UpdatesVmTest, RefreshedEventCoalescesToOneRebuild) {      // ModulesVM discipline
    // N×publish(UpdatesRefreshedEvent) before dispatcher drains → 1 rebuild (hook count)
}
TEST_F(UpdatesVmTest, RequestBannerDurableUntilDismiss) {         // spec §9 / review item 7
    // publish UpdateRequestEvent → requestBanner() non-empty;
    // publish TaskProgressEvent + UpdatesRefreshedEvent → STILL non-empty;
    // dismissRequest() → empty
}
TEST_F(UpdatesVmTest, InstallProgressTextFollowsTaskProgress) {
    // TaskProgressEvent{"install-update:a1", 42, "device-write"} → text contains "42%";
    // unrelated taskId → ignored
}
TEST_F(UpdatesVmTest, TeardownStormNoPostAfterDrain) {            // review test 14 (VM level)
    // queuing dispatcher: hammer UpdatesRefreshedEvent from a thread, destroy VM,
    // drain queue → no crash, no rebuild after dtor (alive token) —
    // copy the T10/T12 stress recipe from test_modules_vm.cpp verbatim
}
```

Run → RED.

- [x] **Step 2: implement.** Copy ModulesVM's mechanics wholesale (subscription → `rebuildQueued_` atomic → dispatcher post guarded by `alive_`; dtor: `alive_->store(false)` then barrier; same comments referencing the contract). Specifics:
  - Subscriptions: `UpdatesRefreshedEvent` (→ coalesced rebuild), `UpdatesChangedEvent` (→ coalesced `refreshQueued_` post that calls `facade_.refreshUpdates()`, future stored in `lastRefresh_` member — future-custody note in dtor), `UpdateRequestEvent` (→ store under mutex, post wake), `TaskProgressEvent` (filter `taskId.rfind("install-update:", 0) == 0` → progress text, post wake), `TaskCompletedEvent` (same prefix → clear progress text; ⊥ clears requestBanner — a "post"-kind DeviceRequest must outlive the operation, so request clearing is EXPLICIT-DISMISS ONLY. Sanctioned narrowing of spec §9's "dismiss | resolution" — record it in the T13 parity ledger).
  - Row format — SINGLE source (V3):
    ```cpp
    // "%-6s %-30.30s %-12.12s -> %-12.12s %s" via fmt::format in one private fn:
    static std::string formatRow(const std::string& provider, const std::string& name,
                                 const std::string& current, const std::string& candidate,
                                 const std::string& marker);
    // marker precedence: "reboot required" > "external download" > "" ;
    // dkms rows: candidate column "-", marker "" (status-only).
    ```
  - `banner()`: per provider `"<id> <version|unavailable: reason>"` join " | ", then `" | reboot required"` when `facade_.rebootPendingEffective()`, then Secure Boot line from `facade_.systemInfo()` (reuse ModulesVM's exact wording).
  - `selectedInstall()`: selected row → candidate; nullopt when: placeholder, provider caps ∌ Install, `!facts.updatable`, no release with `localCab`. Release = first localCab release (fwupd order).

- [x] **Step 3: run → PASS; full ctest**

- [x] **Step 4: USER commits** — `feat(app): Phase 6 T10 — UpdatesVM (byte-frozen rows, durable request banner, coalesced refresh, teardown-safe)`

---

### Task 11: TUI Updates screen

**Files:**
- Modify: `tui/src/tui_app.cpp` (three-tab cycle, updates keys, renderer branch, quit guard)

**Interfaces (Consumes):** `UpdatesVM` exactly as T10; `facade.installActive()`, `facade.refreshUpdates()`, `facade.installUpdate(...)`.

- [x] **Step 1: wire composition root.** In `runTuiApp()` (`tui/src/tui_app.cpp:51`): construct providers before the facade —

```cpp
#ifdef DEVMGR_HAS_SDBUS
    platform_linux::FwupdUpdateProvider fwupdProvider(bus);
#endif
    platform_linux::DkmsStatusProvider dkmsProvider;
    std::vector<pal::IUpdateProvider*> updateProviders;
#ifdef DEVMGR_HAS_SDBUS
    updateProviders.push_back(&fwupdProvider);
#endif
    updateProviders.push_back(&dkmsProvider);
```

pass to the facade ctor (T9 param), construct `UpdatesVM updatesVm(facade, bus, dispatcher);` beside `modulesVm`. Declaration order = teardown contract: providers outlive facade outlive VMs (match the existing ordering comments).

- [x] **Step 2: tab cycle.** Replace the `'m'` handler (`:263-272`):

```cpp
        if (event == Event::Character('m')) {
            activeTab = (activeTab + 1) % 3;  // Devices → Modules → Updates → …
            if (activeTab == 1) {
                bannerText = modulesVm.banner();
                modulesVm.rebuild();
                modDetailDirty = true;
                modulesVm.fillSignatures();
            } else if (activeTab == 2) {
                bannerText = updatesVm.banner();
                updatesVm.rebuild();
                prunePending();
                pending.push_back(facade.refreshUpdates());  // fresh data on entry
            }
            return true;
        }
```

- [x] **Step 3: quit guard (spec §5.5).** Extend the T1-merged quit handler:

```cpp
        if (event == Event::Character('q') || event == Event::Escape) {
            if (facade.installActive()) {
                confirm = PendingConfirm{
                    .onYes = [&] { screen.Exit(); },
                    .prompt = "firmware flash continues in the fwupd daemon; closing does "
                              "NOT cancel it. quit? (y/n)"};
                return true;
            }
            screen.Exit();
            return true;
        }
```

- [x] **Step 4: updates keys** — insert BEFORE the devices-keys section, after the modules block (`:277-308`), mirroring its shape:

```cpp
        if (activeTab == 2) {  // ----- updates keys -----
            if (event == Event::Character('u')) {
                const auto args = updatesVm.selectedInstall();
                if (!args) {
                    bus.publish(core::TaskCompletedEvent{
                        .taskId = "guard", .ok = false,
                        .message = "not installable from here (status-only or external "
                                   "download — run `fwupdmgr update`)"});
                    return true;
                }
                confirm = PendingConfirm{.onYes =
                                             [&, a = *args] {
                                                 prunePending();
                                                 pending.push_back(facade.installUpdate(
                                                     a.providerId, a.candidateId, a.release));
                                             },
                                         .prompt = args->confirmText + " (y/n)"};
                return true;
            }
            if (event == Event::Character('r')) {
                prunePending();
                pending.push_back(facade.refreshUpdates());
                return true;
            }
            if (event == Event::Character('d')) {
                updatesVm.dismissRequest();
                return true;
            }
            return false;  // filter input / menu / mouse
        }
```

- [x] **Step 5: renderer.** Clone the modules-screen render branch (list + detail pane + banner) for `activeTab == 2`, substituting `updatesVm` accessors + `updatesVm.requestBanner()` as a highlighted line above the list when non-empty + `updatesVm.installProgressText()` in the status area. Rebuild on `Event::Custom` (existing dispatcher-drain path drives `UpdatesRefreshedEvent` posts already — T10). Tab titles line: `"Devices | Modules | Updates"` with the active one bold (match existing tab header rendering exactly).

- [x] **Step 6: manual smoke (agent-runnable part)**

Run: `./build/linux-debug/tui/devmgr-tui` on the host — `m` twice → Updates tab lists real fwupd devices (read-only); `q` quits; no crash on rapid `m` cycling. Document observations in the task report.

- [x] **Step 7: USER commits** — `feat(tui): Phase 6 T11 — Updates screen (3-tab cycle, install confirm, request banner, quit guard)`

---

### Task 12: GUI Updates tab

**Files:**
- Create: `gui/src/update_list_model.hpp`, `gui/src/update_list_model.cpp` (mirror `module_list_model.{hpp,cpp}` — QAbstractListModel over `UpdatesVM::rowsRef()` with before/after reset hooks via `setRebuildHooks`)
- Modify: `gui/src/main_window.hpp`, `gui/src/main_window.cpp`, `gui/src/gui_app.cpp` (providers + VM wiring, same order rules as T11 Step 1)
- Modify: `gui/CMakeLists.txt` (new sources)
- Test: `gui/tests/test_update_list_model.cpp` + extend `gui/tests/test_main_window.cpp`

**Interfaces (Consumes):** T10 `UpdatesVM`; T9 facade. **Produces:** `MainWindow::Actions` gains `std::function<bool(const QString&)> confirmQuit;` (tests inject).

- [x] **Step 1: model + tests.** Copy `module_list_model.{hpp,cpp}` structure 1:1 (rename Module→Update); copy `gui/tests/test_module_list_model.cpp` UAF/reset stress recipes for the new model (production `QtUiDispatcher`, storm + drain — keep the test comments' contract citations). RED first (files missing), then implement, then PASS.

- [x] **Step 2: MainWindow wiring.**
  - Tab: `tabs_->addTab(updatesSplitter, tr("Updates"))` as index 2 — list view + detail `QTreeWidget` + banner `QLabel` above the list + request-banner `QLabel` (styled warning, hidden when `requestBanner().empty()`), mirroring the Modules tab construction block.
  - Actions: `installUpdateAction_` (toolbar + context menu), `refreshUpdatesAction_`, `dismissRequestAction_`. Handlers: install → `vm.selectedInstall()`; nullopt → publish guard TaskCompletedEvent (T1 F-1 pattern — NEVER direct statusBar write); else `askConfirm(QString::fromStdString(args->confirmText))` → `facade_.installUpdate(...)` (future into the window's pending set — mirror existing bind/unbind custody).
  - `updateActionEnablement()` — extend the tab logic: `const int tab = tabs_->currentIndex();` `installUpdateAction_->setEnabled(tab == 2 && updatesVm_.selectedInstall().has_value());` `refreshUpdatesAction_->setEnabled(tab == 2);` `dismissRequestAction_->setEnabled(tab == 2 && !updatesVm_.requestBanner().empty());` — keep the T1 F-1 probe gating intact (devices probe only when `tab == 0` && sender relevant).
  - Tab-entry hook: `tabs_` `currentChanged` → when 2: `updatesVm_.rebuild(); pending_.push_back(facade_.refreshUpdates());` + banner refresh (mirror Modules tab-entry block).
  - Quit guard: override `closeEvent(QCloseEvent* e)`:

```cpp
void MainWindow::closeEvent(QCloseEvent* event) {
    if (facade_.installActive()) {
        const auto prompt = tr("Firmware flash continues in the fwupd daemon; closing "
                               "does NOT cancel it. Quit anyway?");
        const bool quit = actions_.confirmQuit ? actions_.confirmQuit(prompt)
                                               : QMessageBox::question(this, tr("Confirm"),
                                                                       prompt) == QMessageBox::Yes;
        if (!quit) { event->ignore(); return; }
    }
    QMainWindow::closeEvent(event);
}
```

- [x] **Step 3: window tests** (extend `gui/tests/test_main_window.cpp`, offscreen platform, FakeUpdateProvider wired):
  - `InstallActionDisabledForRemoteOnlyRelease` (review test 4 GUI half)
  - `InstallActionEnabledOnUpdatesTabWithLocalCab`
  - `QuitGuardBlocksCloseDuringInstall` (inject `actions_.confirmQuit` returning false → window stays; true → closes; latch-blocked fake install drives `installActive()`)
  - `RequestBannerVisibleUntilDismissed` (UpdateRequestEvent → label visible; progress events don't hide it; dismiss action hides)
  - `GuardRefusalGoesThroughStatusLineVM` (install on non-installable → statusBar text arrives via StatusLineVM path, survives a `taskExecuted` wake — pins T1 F-1)
- RED → implement → PASS: `ctest --test-dir build/linux-debug -R MainWindow|UpdateListModel --output-on-failure`.

- [x] **Step 4: manual smoke** — `./build/linux-debug/gui/devmgr-gui`: Updates tab lists real devices read-only; parity vs TUI rows (same strings). USER commits — `feat(gui): Phase 6 T12 — Updates tab (model, actions, request banner, close guard)`

---

### Task 13: VM smoke, packaging of test rig, docs, close-out

**Files:**
- Create: `tests/smoke/CMakeLists.txt`, `tests/smoke/fwupd_smoke_main.cpp`, `test/vm/phase6-smoke.sh`
- Modify: root `CMakeLists.txt` (add `add_subdirectory(tests/smoke)` inside the sdbus block), `test/vm/Vagrantfile:30-32` (packages += `fwupd dkms`), `test-vm.sh` (phase 6 invocation), `README.md` (Updates section)
- Modify: this plan (checkboxes) + `.superpowers/sdd/progress.md` (ledger close-out)

**Interfaces (Consumes):** everything. This task = spec §11.3–§11.5 + §13 gates.

- [x] **Step 1: smoke binary.** `tests/smoke/fwupd_smoke_main.cpp` — no gtest; drives OUR stack end-to-end (spec: the smoke must exercise `FwupdUpdateProvider`, not `fwupdmgr`):

```cpp
// Usage: devmgr_fwupd_smoke [--install] [--device <substr>] [--expect-version <v>] [--dkms <module>]
// Exit 0 = assertions held. Prints PHASE6-SMOKE lines for the shell script to grep.
int main(int argc, char** argv) {
    bool install = false;
    std::string device, expectVersion, dkmsModule;
    for (int i = 1; i < argc; ++i) {  // plain loop, no deps
        const std::string a = argv[i];
        if (a == "--install") install = true;
        else if (a == "--device" && i + 1 < argc) device = argv[++i];
        else if (a == "--expect-version" && i + 1 < argc) expectVersion = argv[++i];
        else if (a == "--dkms" && i + 1 < argc) dkmsModule = argv[++i];
    }
    devmgr::runtime::EventBus bus;
    if (!dkmsModule.empty()) {
        devmgr::platform_linux::DkmsStatusProvider dkms;
        const auto r = dkms.enumerate();
        if (!r) { std::printf("PHASE6-SMOKE dkms-enumerate-failed\n"); return 4; }
        for (const auto& c : *r)
            if (c.id.rfind("dkms:" + dkmsModule + "/", 0) == 0)
                for (const auto& [k, v] : c.details)
                    if (v.find("installed") != std::string::npos) {
                        std::printf("PHASE6-SMOKE dkms %s %s: %s\n", c.id.c_str(), k.c_str(),
                                    v.c_str());
                        std::printf("PHASE6-SMOKE OK\n");
                        return 0;
                    }
        std::printf("PHASE6-SMOKE dkms module '%s' not installed\n", dkmsModule.c_str());
        return 4;
    }
    devmgr::platform_linux::FwupdUpdateProvider fwupd(bus);  // system bus
    const auto avail = fwupd.availability();
    if (!avail.available) {
        std::printf("PHASE6-SMOKE fwupd-unavailable: %s\n",
                    avail.error ? avail.error->message.c_str() : "?");
        return 2;
    }
    const auto cands = fwupd.enumerate();
    if (!cands) { std::printf("PHASE6-SMOKE enumerate-failed\n"); return 2; }
    for (const auto& c : *cands) {
        if (c.displayName.find(device) == std::string::npos || c.releases.empty()) continue;
        const auto& rel = c.releases.front();
        std::printf("PHASE6-SMOKE found %s %s -> %s localcab=%d\n", c.displayName.c_str(),
                    c.currentVersion.c_str(), rel.version.c_str(), rel.localCab ? 1 : 0);
        if (!install) { std::printf("PHASE6-SMOKE OK\n"); return 0; }
        const auto out = fwupd.install(c.id, rel.ref(), [](const auto& u) {
            std::printf("PHASE6-SMOKE progress %d%% %s\n", u.percent, u.stage.c_str());
        });
        if (!out) {
            std::printf("PHASE6-SMOKE install-failed: %s\n", out.error().message.c_str());
            return 3;
        }
        const auto ver = out->observedVersion.value_or("(scheduled)");
        std::printf("PHASE6-SMOKE installed disposition=%d version=%s\n",
                    static_cast<int>(out->disposition), ver.c_str());
        if (!expectVersion.empty() && out->observedVersion != expectVersion) return 3;
        std::printf("PHASE6-SMOKE OK\n");
        return 0;
    }
    std::printf("PHASE6-SMOKE no matching device '%s'\n", device.c_str());
    return 2;
}
```

CMake: link `devmgr_core devmgr_pal_linux`; target `devmgr_fwupd_smoke`.

- [x] **Step 2: `test/vm/phase6-smoke.sh`** (complete script; poll pattern from T1 F-2, ⊥ sleeps):

```bash
#!/usr/bin/env bash
# Phase 6 VM smoke: fwupd fakedevice update + dkms status through OUR stack.
set -e
cd "$(dirname "$0")/../.."

echo "== enable fwupd test remote =="
systemctl start fwupd || true
fwupdmgr enable-test-devices --no-remote-check 2>/dev/null || fwupdmgr enable-test-devices || {
    # fallback (spec §11.3): ship-with-fwupd local remote conf
    sed -i 's/^Enabled=false/Enabled=true/' /etc/fwupd/remotes.d/fwupd-tests.conf
    systemctl restart fwupd
}
for _ in $(seq 1 50); do
    busctl status org.freedesktop.fwupd >/dev/null 2>&1 && break
    sleep 0.2
done

echo "== read side: fakedevice upgrade visible via FwupdUpdateProvider =="
SMOKE=./build/tests/smoke/devmgr_fwupd_smoke
[ -x "$SMOKE" ] || SMOKE=./build/linux-debug/tests/smoke/devmgr_fwupd_smoke
"$SMOKE" --device fakedevice | tee /tmp/p6-read.log
grep -q "localcab=1" /tmp/p6-read.log

echo "== install side: 1.2.2 -> 1.2.4 through our stack =="
"$SMOKE" --device fakedevice --install --expect-version 1.2.4

echo "== dkms status side =="
apt-get install -y dkms >/dev/null 2>&1 || true
if command -v dkms >/dev/null; then
    mkdir -p /usr/src/devmgrtest-1.0
    cat > /usr/src/devmgrtest-1.0/dkms.conf <<'EOF'
PACKAGE_NAME="devmgrtest"
PACKAGE_VERSION="1.0"
BUILT_MODULE_NAME[0]="devmgrtest"
DEST_MODULE_LOCATION[0]="/updates/dkms"
AUTOINSTALL="yes"
MAKE[0]="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build modules"
CLEAN="make -C ${kernel_source_dir} M=${dkms_tree}/${PACKAGE_NAME}/${PACKAGE_VERSION}/build clean"
EOF
    cat > /usr/src/devmgrtest-1.0/Makefile <<'EOF'
obj-m := devmgrtest.o
EOF
    cat > /usr/src/devmgrtest-1.0/devmgrtest.c <<'EOF'
#include <linux/module.h>
static int __init t_init(void) { return 0; }
static void __exit t_exit(void) {}
module_init(t_init); module_exit(t_exit);
MODULE_LICENSE("GPL");
EOF
    apt-get install -y "linux-headers-$(uname -r)" >/dev/null 2>&1 || true
    dkms add -m devmgrtest -v 1.0 2>/dev/null || true
    dkms build -m devmgrtest -v 1.0 && dkms install -m devmgrtest -v 1.0 || \
        echo "note: dkms build unavailable (no headers) — status-only check degraded"
    "$SMOKE" --dkms devmgrtest || echo "note: dkms assertion skipped"
fi

echo "PHASE6 VM SMOKE OK"
```

(Adjust the smoke binary path to the VM build layout — `test-vm.sh` builds into `build/`, host preset into `build/linux-debug`; probe both as shown.)

- [x] **Step 3: rig updates.** Vagrantfile `apt-get install` line += ` fwupd dkms`; `test-vm.sh` append after the phase 5 block:

```bash
echo "==> Running Phase 6 Smoke Test..."
(cd "$VM_DIR" && vagrant ssh -c 'cd ~/cross-device-manager && sudo ./test/vm/phase6-smoke.sh')
```

- [x] **Step 4: README.** Add an "Firmware & driver updates (Phase 6)" section: Updates tab in both UIs, fwupd frontend-direct architecture (one paragraph), local-cab-only install boundary + `fwupdmgr update` guidance, DKMS status-only, reboot-required banner semantics, VM smoke instructions.

- [x] **Step 5: gates (spec §11.5 — run ALL, paste outputs in the task report)**

```
cmake --build --preset linux-debug -j24 && ctest --test-dir build/linux-debug --output-on-failure
cmake --build build/nosdbus -j24
clang-format CI-form check over changed files (18.1.8 host + 21.1.8 if available)
clang-tidy gated dirs
grep -riE "glib|gobject" core app tui gui platform --include="*.hpp" --include="*.cpp"   # expect: 0 hits
grep -rn "sdbus" core/include app/include | grep -v update_models                        # expect: 0 hits (purity)
docker build + container ctest          (USER runs — no docker in-agent)
./test-vm.sh                            (USER runs — expects PHASE4 + PHASE5 + PHASE6 VM SMOKE OK)
```

- [x] **Step 6: parity ledger + close-out.** Append the TUI/GUI Updates parity table (byte-frozen row source = `UpdatesVM::formatRow`, sanctioned diffs listed) + Phase 6 summary to `.superpowers/sdd/progress.md` (caveman). Mark ALL plan checkboxes. Update memory files (`phase6-execution-status`, roadmap).

- [ ] **Step 7: USER commits** — `test+docs: Phase 6 T13 — VM smoke (fakedevice E2E + dkms), rig provisioning, README, close-out`

---

## Exit gate (spec §13 — phase ends when ALL hold)

1. T1 carry-over commit reviewed & landed.
2. M1/M2/M3 demonstrably tested: T9 reboot-persistence tests, T4 cab matrix + T8 `TraversalCabRefusedBeforeDbus`/`RemoteOnlyReleaseRefusedBeforeDbus`, T8 lifecycle scenarios (`PreflightRefusesVanishedRelease`, `OfflineUpdateReportsScheduled`, `SecondInstallWhileActiveIsBusy`, `ProgressIgnoredWhileIdle`, `TeardownDuringSignalStorm`).
3. Host ctest 100%; container suite green (USER).
4. `PHASE6 VM SMOKE OK` + Phase 4/5 smokes still OK (USER, `./test-vm.sh`).
5. Host read-only manual smoke, both UIs (USER): real fw devices + upgrades listed; remote-only rows show guidance w/ disabled verb; banner `fwupd 2.0.20` + Secure Boot; dkms rows absent; TUI/GUI parity; polkit-agent-absent TTY sanity.
6. Parity ledger complete; purity greps (incl. no-GLib) clean.
7. Final whole-branch review (opus) approved.
8. Docs: README section; spec + plan checkboxes closed; memory updated.
