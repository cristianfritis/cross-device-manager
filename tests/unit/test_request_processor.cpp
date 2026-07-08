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
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    daemon::RequestProcessor processor() {
        return daemon::RequestProcessor(pal_, prober_, authority_, pal_, pal_, *store_, mutex_,
                                        root_.string());
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
                                               mutex_, root_.string());
    auto r = processor.setDeviceEnabled(":1.7", ghost, false);
    ASSERT_TRUE(r.has_value()) << r.error().message;
    const auto entries = store_->entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].key.bus, "usb");        // from the subsystem link
    EXPECT_EQ(entries[0].key.vendorId, "1234");  // 0x stripped
    EXPECT_EQ(entries[0].key.serial, "GH0ST");   // from the serial attr
}
