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

    [[nodiscard]] std::string dir() const { return dir_.string(); }
    void writeStateFile(const std::string& contents) const {
        fs::create_directories(dir_);
        std::ofstream(dir_ / "state.json") << contents;
    }
    // Counts directory entries whose filename starts with `prefix`.
    [[nodiscard]] int countFilesMatching(const std::string& prefix) const {
        std::error_code ec;
        if (!fs::exists(dir_, ec)) return 0;
        int n = 0;
        for (const auto& e : fs::directory_iterator(dir_))
            if (e.path().filename().string().starts_with(prefix)) ++n;
        return n;
    }
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
    // Timestamped quarantine name (T4 m-4) — match by prefix, not exact name.
    EXPECT_EQ(countFilesMatching("state.json.bad"), 1);
}

TEST_F(StateStoreTest, EntryLevelCorruptFileIsMovedAsideAndStoreStartsEmpty) {
    fs::create_directories(dir_);
    std::ofstream(dir_ / "state.json") << R"({"version":1,"entries":[42]})";
    StateStore store(dir_.string());
    ASSERT_TRUE(store.load().has_value());  // load succeeds (empty), evidence kept
    EXPECT_TRUE(store.entries().empty());
    EXPECT_EQ(countFilesMatching("state.json.bad"), 1);
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
