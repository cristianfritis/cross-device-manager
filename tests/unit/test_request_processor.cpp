#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/daemon/authority.hpp"
#include "devmgr/daemon/request_processor.hpp"
#include "devmgr/daemon/request_validation.hpp"
#include "devmgr/daemon/snapshot_service.hpp"
#include "devmgr/daemon/snapshot_store.hpp"
#include "devmgr/daemon/state_store.hpp"
#include "fakes/fake_pal.hpp"

namespace fs = std::filesystem;
using devmgr::core::Error;
using namespace devmgr;

namespace {

class RecordingAuthority final : public devmgr::daemon::IAuthority {
   public:
    devmgr::core::Result<bool> checkAuthorized(const devmgr::daemon::CallerId& caller,
                                               const std::string& actionId) override {
        actions.push_back(actionId);
        callers.push_back(caller);
        return answer;
    }
    std::vector<std::string> actions;
    std::vector<devmgr::daemon::CallerId> callers;
    devmgr::core::Result<bool> answer = true;
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

// The processor canonicalizes and containment-checks paths itself, so the
// tests need a real directory to point at.
class RequestProcessorTest : public ::testing::Test {
   protected:
    fs::path root_;
    std::string devicePath_;
    test::FakePal pal_;
    StubProber prober_;
    RecordingAuthority authority_;
    std::unique_ptr<daemon::StateStore> store_;
    std::unique_ptr<daemon::JsonSnapshotStore> snapStore_;
    std::unique_ptr<daemon::SnapshotService> snapshots_;
    std::mutex mutex_;

    void SetUp() override {
        root_ = fs::temp_directory_path() /
                ("devmgr-reqproc-" + std::to_string(::getpid()) + "-" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        const fs::path device = root_ / "devices/pci0000:00/usb1/1-4";
        fs::create_directories(device);
        devicePath_ = fs::weakly_canonical(device).string();

        // Seed FakePal with the device so enumerate() and setEnabled() find it
        core::Device d;
        d.sysfsPath = devicePath_;
        d.bus = core::BusType::Usb;
        d.vendorId = "1d6b";
        d.productId = "0002";
        d.serial = "abc123";
        pal_.seedDevice(d);

        // Create StateStore on a tmp dir with load() called
        const fs::path stateDir = root_ / "state";
        fs::create_directories(stateDir);
        store_ = std::make_unique<daemon::StateStore>(stateDir.string());
        if (auto loaded = store_->load(); !loaded) {
            // Warn but continue (as per brief)
        }
        snapStore_ = std::make_unique<daemon::JsonSnapshotStore>((root_ / "snapshots").string());
        snapshots_ = std::make_unique<daemon::SnapshotService>(
            *snapStore_, *store_, pal_, pal_, prober_, (root_ / "modprobe.d").string());
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    daemon::RequestProcessor processor() {
        return daemon::RequestProcessor(pal_, prober_, authority_, pal_, pal_, *store_, *snapshots_,
                                        mutex_, root_.string());
    }

    // Fixture helper for the enumerator-fallback test: creates
    // root_/devices/<rel> with the given attr files (one line each) plus a
    // `subsystem` symlink to root_/bus/usb (created once, idempotently), and
    // returns the weakly_canonical path string of the device dir.
    std::string makeDeviceDir(const std::string& rel,
                              const std::vector<std::pair<std::string, std::string>>& attrs) {
        const fs::path dir = root_ / "devices" / rel;
        fs::create_directories(dir);
        for (const auto& [name, value] : attrs) {
            std::ofstream(dir / name) << value;
        }
        fs::create_directories(root_ / "bus/usb");
        fs::create_directory_symlink(root_ / "bus/usb", dir / "subsystem");
        return fs::weakly_canonical(dir).string();
    }
};

}  // namespace

TEST_F(RequestProcessorTest, HappyPathDisablesViaControllerWithCanonicalPath) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.42", devicePath_, false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1u);
    EXPECT_EQ(pal_.setEnabledCalls[0].sysfsPath, devicePath_);
    EXPECT_FALSE(pal_.setEnabledCalls[0].enabled);
    ASSERT_EQ(authority_.actions.size(), 1U);
    EXPECT_EQ(authority_.actions[0], daemon::kActionSetDeviceEnabled);
    EXPECT_EQ(authority_.callers[0], ":1.42");
}

TEST_F(RequestProcessorTest, GuardRefusalShortCircuitsBeforeAuthorityAndController) {
    pal::CriticalityFacts f;
    f.rootBackingPaths = {devicePath_ + "/host0/block/sdb"};
    prober_.next = f;
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", devicePath_, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Conflict);
    EXPECT_EQ(r.error().message, "backs the root filesystem");
    EXPECT_TRUE(authority_.actions.empty());  // no password prompt for a doomed request
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}

TEST_F(RequestProcessorTest, EnableSkipsGuardButStillAuthorizes) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", devicePath_, true);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(prober_.probes, 0);  // re-enabling can't hurt — guard not consulted
    ASSERT_EQ(authority_.actions.size(), 1U);
}

TEST_F(RequestProcessorTest, DeniedAuthorityIsPermissionAndBlocksController) {
    authority_.answer = false;
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", devicePath_, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Permission);
    EXPECT_EQ(r.error().message, "authorization denied");
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}

TEST_F(RequestProcessorTest, AuthorityErrorPropagates) {
    authority_.answer = core::makeError(Error::Code::Io, "polkit unavailable");
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", devicePath_, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}

TEST_F(RequestProcessorTest, PathOutsideRootIsNotFoundBeforeEverything) {
    auto p = processor();
    auto r = p.setDeviceEnabled(":1.1", "/etc/passwd", false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
    EXPECT_EQ(prober_.probes, 0);
    EXPECT_TRUE(authority_.actions.empty());
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
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
    auto r = p.setDeviceEnabled(":1.1", devicePath_, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_TRUE(authority_.actions.empty());
}

TEST_F(RequestProcessorTest, LoadModuleValidatesNameBeforeAnything) {
    auto r = processor().loadModule(":1.7", "../evil");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(r.error().message, "invalid module name");
    EXPECT_TRUE(authority_.actions.empty());  // never authorized
    EXPECT_TRUE(pal_.loadedModules.empty());  // never acted
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
    EXPECT_TRUE(store_->entries().empty());  // spec §6.2: surgical
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
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(r.error().message, "invalid driver name");
}

TEST_F(RequestProcessorTest, ListDisabledDevicesExposesStoreEntries) {
    pal_.unboundDriverResult = std::optional<std::string>{};
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, false).has_value());
    EXPECT_EQ(processor().listDisabledDevices().size(), 1U);
}

namespace {
struct EmptyEnumerator final : devmgr::pal::IDeviceEnumerator {
    devmgr::core::Result<std::vector<devmgr::core::Device>> enumerate() override {
        return std::vector<devmgr::core::Device>{};
    }
};
}  // namespace

TEST_F(RequestProcessorTest, EnableMatchesReplugAtNewSysfsPathByDeviceKey) {
    // Disable at path A — persists an entry keyed by (bus, vendor, product,
    // serial).
    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", devicePath_, false).has_value());
    ASSERT_EQ(store_->entries().size(), 1U);

    // Simulate a daemon-down replug: the SAME device identity reappears at a
    // NEW sysfs path B. The store's lastSysfsPath still says A — applyEnable
    // must still find (and remove) the entry via the device key, not just the
    // stale path (Phase 5 review F-3).
    const std::string pathB = makeDeviceDir(
        "usb1/1-9", {{"idVendor", "1d6b"}, {"idProduct", "0002"}, {"serial", "abc123"}});
    core::Device moved;
    moved.sysfsPath = pathB;
    moved.bus = core::BusType::Usb;
    moved.vendorId = "1d6b";
    moved.productId = "0002";
    moved.serial = "abc123";
    pal_.seedDevice(moved);  // controller must accept setEnabled at path B too

    ASSERT_TRUE(processor().setDeviceEnabled(":1.7", pathB, true).has_value());
    EXPECT_TRUE(store_->entries().empty());
}

TEST_F(RequestProcessorTest, DisableOfUnenumeratedDeviceFallsBackToSysfsKey) {
    // Device dir exists on disk (validation passes, FakePal-as-controller is
    // seeded) but the enumerator sees NOTHING — the key must come from attrs.
    // makeDeviceDir(rel, attrs) = fixture helper: creates root_/devices/<rel>
    // with the given attr files + a `subsystem` symlink to root_/bus/usb.
    const std::string ghost = makeDeviceDir(
        "usb9/9-1", {{"idVendor", "0x1234"}, {"idProduct", "0x5678"}, {"serial", "GH0ST"}});
    devmgr::core::Device seeded;
    seeded.sysfsPath = ghost;
    pal_.seedDevice(seeded);  // controller side only
    EmptyEnumerator empty;
    devmgr::daemon::RequestProcessor processor(pal_, prober_, authority_, pal_, empty, *store_,
                                               *snapshots_, mutex_, root_.string());
    auto r = processor.setDeviceEnabled(":1.7", ghost, false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    const auto entries = store_->entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].key.bus, "usb");        // from the subsystem link
    EXPECT_EQ(entries[0].key.vendorId, "1234");  // 0x stripped
    EXPECT_EQ(entries[0].key.serial, "GH0ST");   // from the serial attr
}

// Phase 7: every mutating verb snapshots BEFORE writing (fail-closed hook).

TEST_F(RequestProcessorTest, DisableTakesAutoSnapshotOfPreMutationState) {
    auto p = processor();
    ASSERT_TRUE(p.setDeviceEnabled(":1.7", devicePath_, false).has_value());

    auto metas = snapshots_->list();
    ASSERT_TRUE(metas.has_value());
    ASSERT_EQ(metas->size(), 1u);
    EXPECT_EQ((*metas)[0].reason.verb, "SetDeviceEnabled");
    EXPECT_EQ((*metas)[0].reason.subject, devicePath_);
    // Snapshot-before-write ordering: the captured payload is the state
    // BEFORE the disable — no entries yet.
    auto snap = snapStore_->read((*metas)[0].id);
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->payload.entries.empty());
    ASSERT_EQ(store_->entries().size(), 1u);  // ...while the disable did land
}

TEST_F(RequestProcessorTest, UnwritableSnapshotDirBlocksMutationFailClosed) {
    // Replace the snapshots dir with a regular FILE so every store write fails.
    fs::remove_all(root_ / "snapshots");
    std::ofstream(root_ / "snapshots") << "not a directory";

    auto p = processor();
    auto r = p.setDeviceEnabled(":1.7", devicePath_, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::Io);
    EXPECT_TRUE(store_->entries().empty());     // no state change
    EXPECT_TRUE(pal_.setEnabledCalls.empty());  // no hardware write
}

TEST_F(RequestProcessorTest, ModuleAndDriverVerbsSnapshotToo) {
    auto p = processor();
    pal_.seedLoadedModule({.name = "snd_test", .sizeBytes = 1, .refCount = 0, .holders = {}});
    ASSERT_TRUE(p.loadModule(":1.7", "snd_test").has_value());
    ASSERT_TRUE(p.unloadModule(":1.7", "snd_test").has_value());
    ASSERT_TRUE(p.unbindDriver(":1.7", devicePath_).has_value());

    auto metas = snapshots_->list();
    ASSERT_TRUE(metas.has_value());
    // State never changes between verbs, so hash-dedupe collapses all three
    // snapshots into one — but each verb DID run the hook (newest reason wins
    // only for the first write; dedupe keeps the original).
    ASSERT_EQ(metas->size(), 1u);
    EXPECT_EQ((*metas)[0].reason.verb, "LoadModule");
    EXPECT_EQ((*metas)[0].reason.subject, "snd_test");
}

// Phase 7 / ApiVersion 3: snapshot verb dispatch, authorization, validation.

TEST_F(RequestProcessorTest, SnapshotCreateUsesManageSnapshotsAction) {
    auto p = processor();
    auto id = p.snapshotCreate(":1.9", "my label");
    ASSERT_TRUE(id.has_value());
    ASSERT_EQ(authority_.actions.size(), 1u);
    EXPECT_EQ(authority_.actions[0], daemon::kActionManageSnapshots);
    auto metas = p.snapshotList();
    ASSERT_TRUE(metas.has_value());
    ASSERT_EQ(metas->size(), 1u);
    EXPECT_EQ((*metas)[0].trigger, core::SnapshotTrigger::Manual);
    EXPECT_EQ((*metas)[0].reason.subject, "my label");
}

TEST_F(RequestProcessorTest, SnapshotListNeedsNoAuthorization) {
    authority_.answer = false;  // even a denied caller may list
    auto p = processor();
    auto metas = p.snapshotList();
    ASSERT_TRUE(metas.has_value());
    EXPECT_TRUE(authority_.actions.empty());
}

TEST_F(RequestProcessorTest, SnapshotMutatingVerbsDeniedWithoutAuthorization) {
    authority_.answer = false;
    auto p = processor();
    EXPECT_EQ(p.snapshotCreate(":1.9", "x").error().code, Error::Code::Permission);
    EXPECT_EQ(p.snapshotRestore(":1.9", std::string(64, 'a')).error().code,
              Error::Code::Permission);
    EXPECT_EQ(p.snapshotDelete(":1.9", std::string(64, 'a')).error().code, Error::Code::Permission);
}

TEST_F(RequestProcessorTest, SnapshotIdValidationRejectsTraversalBeforeAuth) {
    auto p = processor();
    auto r = p.snapshotRestore(":1.9", "../../../etc/passwd");
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    EXPECT_TRUE(authority_.actions.empty());  // rejected before any auth prompt
    EXPECT_EQ(p.snapshotDelete(":1.9", "DEADBEEF").error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(p.snapshotCreate(":1.9", std::string("bad\x01label")).error().code,
              Error::Code::InvalidArgs);
}

TEST_F(RequestProcessorTest, SnapshotRestoreOfUnknownIdIsNotFound) {
    auto p = processor();
    auto r = p.snapshotRestore(":1.9", std::string(64, '0'));
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::NotFound);
}

TEST_F(RequestProcessorTest, SnapshotFullLifecycleThroughVerbs) {
    auto p = processor();
    auto clean = p.snapshotCreate(":1.9", "clean");
    ASSERT_TRUE(clean.has_value());
    ASSERT_TRUE(p.setDeviceEnabled(":1.9", devicePath_, false).has_value());
    ASSERT_EQ(store_->entries().size(), 1u);

    auto outcome = p.snapshotRestore(":1.9", *clean);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(store_->entries().empty());  // back to clean

    ASSERT_TRUE(p.snapshotDelete(":1.9", *clean).has_value());
    auto metas = p.snapshotList();
    ASSERT_TRUE(metas.has_value());
    for (const auto& m : *metas) EXPECT_NE(m.id, *clean);
}

// ---- Central validation layer (daemon-hardening spec) ----
// Every verb caps and charset-checks its arguments before guard/authorize/act,
// so hostile input never prompts for a password and never changes state.

TEST_F(RequestProcessorTest, OversizedStringArgumentsRefusedOnEveryVerb) {
    auto p = processor();
    const std::string huge(4U << 20U, 'a');  // 4 MiB
    const auto expect = [&](const Error& e) {
        EXPECT_EQ(e.code, Error::Code::InvalidArgs) << e.message;
    };
    expect(p.setDeviceEnabled(":1.9", huge, false).error());
    expect(p.loadModule(":1.9", huge).error());
    expect(p.unloadModule(":1.9", huge).error());
    expect(p.bindDriver(":1.9", huge, "dummy").error());
    expect(p.bindDriver(":1.9", devicePath_, huge).error());
    expect(p.unbindDriver(":1.9", huge).error());
    expect(p.snapshotCreate(":1.9", huge).error());
    expect(p.snapshotRestore(":1.9", huge).error());
    expect(p.snapshotDelete(":1.9", huge).error());
    // Nothing was authorized and nothing was acted on.
    EXPECT_TRUE(authority_.actions.empty());
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
    EXPECT_TRUE(pal_.loadedModules.empty());
    EXPECT_TRUE(store_->entries().empty());
}

TEST_F(RequestProcessorTest, OversizedCallerRefusedBeforeAuthority) {
    auto p = processor();
    const std::string huge(512, ':');
    auto r = p.setDeviceEnabled(huge, devicePath_, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    EXPECT_TRUE(authority_.actions.empty());
}

TEST_F(RequestProcessorTest, EmptyArgumentsRefused) {
    auto p = processor();
    EXPECT_EQ(p.loadModule(":1.9", "").error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(p.unbindDriver(":1.9", "").error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(p.snapshotRestore(":1.9", "").error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(p.setDeviceEnabled("", devicePath_, false).error().code, Error::Code::InvalidArgs);
    EXPECT_TRUE(authority_.actions.empty());
}

// A NUL byte would silently truncate the path on the way to sysfs, so it is
// refused rather than canonicalized.
TEST_F(RequestProcessorTest, EmbeddedNulInPathRefused) {
    auto p = processor();
    const std::string sneaky = devicePath_ + std::string("\0/../../etc", 11);
    auto r = p.setDeviceEnabled(":1.9", sneaky, false);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error().code, Error::Code::InvalidArgs);
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}

TEST_F(RequestProcessorTest, ModuleNameLengthCapEnforcedAtBoundary) {
    auto p = processor();
    const std::string atCap(devmgr::daemon::validation::kNameMaxLength, 'm');
    const std::string overCap(devmgr::daemon::validation::kNameMaxLength + 1, 'm');
    EXPECT_EQ(p.unloadModule(":1.9", overCap).error().code, Error::Code::InvalidArgs);
    // At the cap the name is well-formed, so it passes validation and fails
    // later for a real reason (no such module loaded) — proving the cap is
    // off-by-one clean rather than rejecting everything.
    EXPECT_EQ(p.unloadModule(":1.9", atCap).error().code, Error::Code::NotFound);
}

TEST_F(RequestProcessorTest, ControlBytesInLabelRefusedButUtf8Accepted) {
    auto p = processor();
    EXPECT_EQ(p.snapshotCreate(":1.9", "line\nbreak").error().code, Error::Code::InvalidArgs);
    EXPECT_TRUE(p.snapshotCreate(":1.9", "café — ünïcode ✓").has_value());
}

TEST_F(RequestProcessorTest, UppercaseHexSnapshotIdRefused) {
    auto p = processor();
    // Ids are lowercase-hex only: the charset is a path-traversal guard, so
    // "close enough" spellings are refused rather than normalized.
    EXPECT_EQ(p.snapshotDelete(":1.9", std::string(64, 'A')).error().code,
              Error::Code::InvalidArgs);
    EXPECT_EQ(p.snapshotDelete(":1.9", "abc/../def").error().code, Error::Code::InvalidArgs);
}

// ApiVersion 4: SnapshotDiff. A read verb — unprivileged like List, validated
// like every other verb, and never a mutation.

TEST_F(RequestProcessorTest, SnapshotDiffNeedsNoAuthorization) {
    auto p = processor();
    auto created = p.snapshotCreate(":1.9", "base");
    ASSERT_TRUE(created.has_value());

    authority_.actions.clear();
    authority_.answer = false;  // every polkit check now refuses
    EXPECT_EQ(p.snapshotRestore(":1.9", *created).error().code, Error::Code::Permission);
    authority_.actions.clear();

    auto diff = p.snapshotDiff(*created, "");
    ASSERT_TRUE(diff.has_value()) << diff.error().message;
    EXPECT_TRUE(authority_.actions.empty());  // read parity with List
}

TEST_F(RequestProcessorTest, SnapshotDiffNamesTheDeviceDisabledSinceTheSnapshot) {
    auto p = processor();
    auto clean = p.snapshotCreate(":1.9", "clean");
    ASSERT_TRUE(clean.has_value());
    ASSERT_TRUE(p.setDeviceEnabled(":1.9", devicePath_, false).has_value());

    auto diff = p.snapshotDiff(*clean, "");  // empty target = live state
    ASSERT_TRUE(diff.has_value()) << diff.error().message;
    EXPECT_EQ(diff->baseId, *clean);
    EXPECT_TRUE(diff->targetId.empty());
    ASSERT_EQ(diff->entries.size(), 1u);
    EXPECT_EQ(diff->entries[0].kind, core::kDiffKindDevice);
    EXPECT_EQ(diff->entries[0].before, core::kDiffStateEnabled);
    EXPECT_EQ(diff->entries[0].after, "disabled (authorized)");
}

TEST_F(RequestProcessorTest, SnapshotDiffOfASnapshotAgainstItselfIsEmpty) {
    auto p = processor();
    auto id = p.snapshotCreate(":1.9", "same");
    ASSERT_TRUE(id.has_value());
    auto diff = p.snapshotDiff(*id, *id);
    ASSERT_TRUE(diff.has_value());
    EXPECT_TRUE(diff->identical());
    EXPECT_EQ(diff->targetId, *id);
}

TEST_F(RequestProcessorTest, SnapshotDiffValidatesBothIdsAndRefusesUnknownOnes) {
    auto p = processor();
    EXPECT_EQ(p.snapshotDiff("../evil", "").error().code, Error::Code::InvalidArgs);
    EXPECT_EQ(p.snapshotDiff(std::string(64, '0'), "../evil").error().code,
              Error::Code::InvalidArgs);
    EXPECT_EQ(p.snapshotDiff("", "").error().code, Error::Code::InvalidArgs);
    // Well-formed but absent: NotFound, not InvalidArgs.
    EXPECT_EQ(p.snapshotDiff(std::string(64, '0'), "").error().code, Error::Code::NotFound);
}

TEST_F(RequestProcessorTest, SnapshotDiffTakesNoSnapshotAndChangesNoState) {
    auto p = processor();
    auto id = p.snapshotCreate(":1.9", "only");
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(p.snapshotDiff(*id, "").has_value());
    auto metas = p.snapshotList();
    ASSERT_TRUE(metas.has_value());
    EXPECT_EQ(metas->size(), 1u);  // a read verb never creates a safety snapshot
    EXPECT_TRUE(store_->entries().empty());
}
