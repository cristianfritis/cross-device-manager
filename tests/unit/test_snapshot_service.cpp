#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "devmgr/daemon/snapshot_service.hpp"
#include "devmgr/services/device_key.hpp"
#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_pal.hpp"

namespace fs = std::filesystem;
using devmgr::core::BusType;
using devmgr::core::Device;
using devmgr::core::DeviceStatus;
using devmgr::core::DisabledDeviceEntry;
using devmgr::core::Error;
using devmgr::core::SnapshotTrigger;
using devmgr::daemon::JsonSnapshotStore;
using devmgr::daemon::SnapshotService;
using devmgr::daemon::StateStore;

class SnapshotServiceTest : public ::testing::Test {
   protected:
    fs::path base_;
    devmgr::test::FakePal pal_;
    devmgr::test::FakeCriticalityProber prober_;
    std::unique_ptr<StateStore> state_;
    std::unique_ptr<JsonSnapshotStore> store_;

    void SetUp() override {
        base_ = fs::temp_directory_path() /
                ("devmgr-snapsvc-" +
                 std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(base_);
        fs::create_directories(base_ / "state");
        fs::create_directories(base_ / "snapshots");
        fs::create_directories(base_ / "modprobe.d");
        state_ = std::make_unique<StateStore>((base_ / "state").string());
        ASSERT_TRUE(state_->load().has_value());
        store_ = std::make_unique<JsonSnapshotStore>((base_ / "snapshots").string());
    }
    void TearDown() override { fs::remove_all(base_); }

    SnapshotService service() {
        return SnapshotService(*store_, *state_, pal_, pal_, prober_,
                               (base_ / "modprobe.d").string());
    }
    void writeModprobeFile(const std::string& name, const std::string& content) const {
        std::ofstream(base_ / "modprobe.d" / name) << content;
    }
    [[nodiscard]] bool modprobeFileExists(const std::string& name) const {
        return fs::exists(base_ / "modprobe.d" / name);
    }

    static Device usbDevice(const std::string& path, const std::string& serial) {
        Device d;
        d.bus = BusType::Usb;
        d.sysfsPath = path;
        d.vendorId = "046d";
        d.productId = "c52b";
        d.serial = serial;
        d.status = DeviceStatus::Active;
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

TEST_F(SnapshotServiceTest, CreateCapturesEntriesAndOnlyDevmgrModprobeFiles) {
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    ASSERT_TRUE(state_->upsert(entryFor(d)).has_value());
    writeModprobeFile("devmgr-blacklist.conf", "blacklist pcspkr\n");
    writeModprobeFile("devmgr-options.conf", "options snd_hda_intel power_save=1\n");
    writeModprobeFile("99-vendor.conf", "blacklist nouveau\n");  // not devmgr-owned

    auto svc = service();
    auto id = svc.create(SnapshotTrigger::Manual, {"", "test label"});
    ASSERT_TRUE(id.has_value());

    auto snap = store_->read(*id);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->payload.entries.size(), 1u);
    EXPECT_EQ(snap->payload.entries[0].key.serial, "AB12");
    ASSERT_EQ(snap->payload.modprobeFiles.size(), 2u);
    EXPECT_EQ(snap->payload.modprobeFiles.at("devmgr-blacklist.conf"), "blacklist pcspkr\n");
    EXPECT_FALSE(snap->payload.modprobeFiles.contains("99-vendor.conf"));
}

TEST_F(SnapshotServiceTest, UnchangedStateDedupesToSameId) {
    auto svc = service();
    auto id1 = svc.create(SnapshotTrigger::Auto, {"SetDeviceEnabled", "d1"});
    auto id2 = svc.create(SnapshotTrigger::Auto, {"LoadModule", "snd"});
    ASSERT_TRUE(id1 && id2);
    EXPECT_EQ(*id1, *id2);
    auto list = svc.list();
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->size(), 1u);
}

TEST_F(SnapshotServiceTest, DeleteHeadMovesHeadToParent) {
    auto svc = service();
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    auto id1 = svc.create(SnapshotTrigger::Auto, {"SetDeviceEnabled", "d1"});
    ASSERT_TRUE(state_->upsert(entryFor(d)).has_value());
    auto id2 = svc.create(SnapshotTrigger::Auto, {"SetDeviceEnabled", "d2"});
    ASSERT_TRUE(id1 && id2);

    ASSERT_TRUE(svc.remove(*id2).has_value());
    auto head = store_->head();
    ASSERT_TRUE(head.has_value());
    EXPECT_EQ(*head, *id1);
}

TEST_F(SnapshotServiceTest, MissingModprobeDirIsTolerated) {
    SnapshotService svc(*store_, *state_, pal_, pal_, prober_, (base_ / "does-not-exist").string());
    auto id = svc.create(SnapshotTrigger::Manual, {"", ""});
    ASSERT_TRUE(id.has_value());
    auto snap = store_->read(*id);
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->payload.modprobeFiles.empty());
}

// Spec scenario: device disabled AFTER snapshot S -> restore S removes the
// entry and attempts a re-enable.
TEST_F(SnapshotServiceTest, RestoreReEnablesDeviceDisabledAfterSnapshot) {
    auto svc = service();
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);
    auto clean = svc.create(SnapshotTrigger::Manual, {"", "clean"});
    ASSERT_TRUE(clean.has_value());

    auto e = entryFor(d);
    e.lastDriver = "usbhid";
    ASSERT_TRUE(state_->upsert(e).has_value());  // device disabled after S

    auto outcome = svc.restore(*clean);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(state_->entries().empty());
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1u);
    EXPECT_EQ(pal_.setEnabledCalls[0].sysfsPath, d.sysfsPath);
    EXPECT_TRUE(pal_.setEnabledCalls[0].enabled);
    EXPECT_EQ(pal_.setEnabledCalls[0].hint, "usbhid");
    ASSERT_EQ(outcome->items.size(), 1u);
    EXPECT_EQ(outcome->items[0].action, "re-enable");
    EXPECT_EQ(outcome->items[0].status, "ok");
    EXPECT_FALSE(outcome->safetySnapshotId.empty());
}

// Spec scenario: entry present in S was re-enabled after S -> restore S
// re-applies the disable through the enforcement path, guard willing.
TEST_F(SnapshotServiceTest, RestoreReAppliesDisableRemovedAfterSnapshot) {
    auto svc = service();
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);  // status Active => needs re-apply
    ASSERT_TRUE(state_->upsert(entryFor(d)).has_value());
    auto snap = svc.create(SnapshotTrigger::Manual, {"", "disabled-state"});
    ASSERT_TRUE(snap.has_value());

    ASSERT_TRUE(state_->remove(entryFor(d).key).has_value());  // re-enabled after S

    auto outcome = svc.restore(*snap);
    ASSERT_TRUE(outcome.has_value());
    ASSERT_EQ(state_->entries().size(), 1u);
    ASSERT_EQ(pal_.setEnabledCalls.size(), 1u);
    EXPECT_EQ(pal_.setEnabledCalls[0].sysfsPath, d.sysfsPath);
    EXPECT_FALSE(pal_.setEnabledCalls[0].enabled);
    ASSERT_EQ(outcome->items.size(), 1u);
    EXPECT_EQ(outcome->items[0].action, "re-apply-disable");
    EXPECT_EQ(outcome->items[0].status, "ok");
}

// Spec scenario: convergence would disable a device that now hosts the root
// disk -> guard-suspended entry, device stays enabled, refusal reported.
TEST_F(SnapshotServiceTest, GuardRefusalIsReportedNotBypassed) {
    auto svc = service();
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);
    ASSERT_TRUE(state_->upsert(entryFor(d)).has_value());
    auto snap = svc.create(SnapshotTrigger::Manual, {"", "disabled-state"});
    ASSERT_TRUE(snap.has_value());
    ASSERT_TRUE(state_->remove(entryFor(d).key).has_value());

    prober_.next = devmgr::pal::CriticalityFacts{.rootBackingPaths = {d.sysfsPath + "/disk"}};
    auto outcome = svc.restore(*snap);
    ASSERT_TRUE(outcome.has_value());  // partial convergence is SUCCESS
    EXPECT_TRUE(pal_.setEnabledCalls.empty());
    ASSERT_EQ(outcome->items.size(), 1u);
    EXPECT_EQ(outcome->items[0].status, "guard-refused");
    EXPECT_EQ(outcome->items[0].detail, "backs the root filesystem");
    ASSERT_EQ(state_->entries().size(), 1u);
    EXPECT_TRUE(state_->entries()[0].guardSuspended);
}

// Spec scenario: a wrong restore is undone by restoring the safety snapshot
// taken at restore time.
TEST_F(SnapshotServiceTest, UndoRestoreViaSafetySnapshot) {
    auto svc = service();
    const auto d = usbDevice("/sys/devices/usb2/2-1", "AB12");
    pal_.seedDevice(d);
    auto empty = svc.create(SnapshotTrigger::Manual, {"", "empty"});
    ASSERT_TRUE(empty.has_value());
    ASSERT_TRUE(state_->upsert(entryFor(d)).has_value());
    writeModprobeFile("devmgr-blacklist.conf", "blacklist pcspkr\n");

    auto wrong = svc.restore(*empty);  // oops — wipes the disable + the file
    ASSERT_TRUE(wrong.has_value());
    EXPECT_TRUE(state_->entries().empty());
    EXPECT_FALSE(modprobeFileExists("devmgr-blacklist.conf"));

    auto undo = svc.restore(wrong->safetySnapshotId);
    ASSERT_TRUE(undo.has_value());
    ASSERT_EQ(state_->entries().size(), 1u);
    EXPECT_EQ(state_->entries()[0].key, entryFor(d).key);
    EXPECT_TRUE(modprobeFileExists("devmgr-blacklist.conf"));
    EXPECT_EQ(store_->read(wrong->safetySnapshotId)->meta.reason.verb, "SnapshotRestore");
}

TEST_F(SnapshotServiceTest, RestoreRewritesModprobeFilesConfigLevel) {
    auto svc = service();
    writeModprobeFile("devmgr-old.conf", "blacklist old\n");
    auto snap = svc.create(SnapshotTrigger::Manual, {"", "with-old"});
    ASSERT_TRUE(snap.has_value());
    ASSERT_TRUE(fs::remove(base_ / "modprobe.d" / "devmgr-old.conf"));
    writeModprobeFile("devmgr-new.conf", "blacklist new\n");

    auto outcome = svc.restore(*snap);
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(modprobeFileExists("devmgr-old.conf"));
    EXPECT_FALSE(modprobeFileExists("devmgr-new.conf"));
    ASSERT_EQ(outcome->items.size(), 2u);
    for (const auto& item : outcome->items) {
        EXPECT_EQ(item.status, "ok");
        EXPECT_NE(item.detail.find("config-level"), std::string::npos);
    }
}

TEST_F(SnapshotServiceTest, RestoreUnknownIdFailsWithoutTakingSnapshot) {
    auto svc = service();
    ASSERT_TRUE(svc.create(SnapshotTrigger::Manual, {"", "baseline"}).has_value());
    const auto before = svc.list();
    ASSERT_TRUE(before.has_value());

    auto outcome = svc.restore(std::string(64, '0'));
    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().code, Error::Code::NotFound);
    auto after = svc.list();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->size(), before->size());  // no safety snapshot taken
}
