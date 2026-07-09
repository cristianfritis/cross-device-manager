#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>

#include "devmgr/daemon/enforcement_service.hpp"
#include "devmgr/services/device_key.hpp"
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
               ("devmgr-enforce-" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
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

    // A real on-disk sysfs-ish device dir: `authorized` + a `subsystem` link to
    // a usb bus dir. Returns its path.
    std::string makeSysfsDevice(const std::string& rel, const std::string& authorized) {
        const fs::path bus = dir_ / "bus/usb";
        fs::create_directories(bus);
        const fs::path dev = dir_ / "devices" / rel;
        fs::create_directories(dev);
        std::ofstream(dev / "authorized") << authorized;
        std::error_code ec;
        fs::create_directory_symlink(bus, dev / "subsystem", ec);
        return dev.string();
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
    pal_.seedDevice(moved);
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

namespace {
struct EmptyEnumerator final : devmgr::pal::IDeviceEnumerator {
    devmgr::core::Result<std::vector<devmgr::core::Device>> enumerate() override {
        return std::vector<devmgr::core::Device>{};
    }
};
}  // namespace

TEST_F(EnforcementServiceTest, SweepFallsBackToSysfsWhenEnumeratorMissesDevice) {
    const std::string path = makeSysfsDevice("usb2/2-1", "1");  // kernel re-enabled it
    Device seeded;
    seeded.sysfsPath = path;
    pal_.seedDevice(seeded);  // controller side only — the enumerator below sees nothing
    auto e = entryFor(usbDevice(path, "AB12"));
    ASSERT_TRUE(store_->upsert(e).has_value());
    EmptyEnumerator empty;
    EnforcementService svc(empty, pal_, prober_, *store_, mutex_);
    svc.sweep();
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1U);
    EXPECT_EQ(pal_.setEnabledCalls[0].sysfsPath, path);
    EXPECT_FALSE(pal_.setEnabledCalls[0].enabled);
}

TEST_F(EnforcementServiceTest, SweepFallbackSkipsDeviceAlreadyDisabledOnDisk) {
    // authorized == "0" => deviceFromSysfs reports Disabled => nothing to re-apply.
    const std::string path = makeSysfsDevice("usb2/2-1", "0");
    Device seeded;
    seeded.sysfsPath = path;
    pal_.seedDevice(seeded);
    ASSERT_TRUE(store_->upsert(entryFor(usbDevice(path, "AB12"))).has_value());
    EmptyEnumerator empty;
    EnforcementService svc(empty, pal_, prober_, *store_, mutex_);
    svc.sweep();
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
}

TEST_F(EnforcementServiceTest, SweepFallbackIgnoresVanishedPath) {
    // Entry survives, but the device is physically gone: no re-apply, no throw.
    ASSERT_TRUE(
        store_->upsert(entryFor(usbDevice((dir_ / "devices/gone").string(), "AB12"))).has_value());
    EmptyEnumerator empty;
    EnforcementService svc(empty, pal_, prober_, *store_, mutex_);
    EXPECT_NO_THROW(svc.sweep());
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
    EXPECT_EQ(store_->entries().size(), 1U);
}
