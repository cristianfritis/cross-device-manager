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
    return DisabledDeviceEntry{.key = DeviceKey{.bus = "usb",
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
               ("devmgr-store-" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
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

TEST_F(StateStoreTest, EntryLevelCorruptFileIsMovedAsideAndStoreStartsEmpty) {
    fs::create_directories(dir_);
    std::ofstream(dir_ / "state.json") << R"({"version":1,"entries":[42]})";
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
