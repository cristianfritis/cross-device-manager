#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "devmgr/daemon/snapshot_store.hpp"

namespace fs = std::filesystem;
using devmgr::core::DeviceKey;
using devmgr::core::DisabledDeviceEntry;
using devmgr::core::Error;
using devmgr::core::SnapshotHealth;
using devmgr::core::SnapshotPayload;
using devmgr::core::SnapshotReason;
using devmgr::core::SnapshotTrigger;
using devmgr::daemon::JsonSnapshotStore;

namespace {
SnapshotPayload payloadWith(const std::string& serial) {
    SnapshotPayload p;
    p.entries.push_back(DisabledDeviceEntry{.key = DeviceKey{.bus = "usb",
                                                             .vendorId = "046d",
                                                             .productId = "c52b",
                                                             .serial = serial,
                                                             .position = "2-1"},
                                            .mechanism = "authorized",
                                            .lastDriver = "usbhid",
                                            .lastSysfsPath = "/sys/devices/pci0000:00/usb2/2-1",
                                            .disabledAtUtc = 1780000000,
                                            .guardSuspended = false});
    p.modprobeFiles["devmgr-blacklist.conf"] = "blacklist pcspkr\n";
    return p;
}
}  // namespace

class JsonSnapshotStoreTest : public ::testing::Test {
   protected:
    fs::path dir_;
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("devmgr-snapstore-" +
                std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    [[nodiscard]] JsonSnapshotStore store() const { return JsonSnapshotStore(dir_.string()); }
    [[nodiscard]] std::string fileContents(const std::string& name) const {
        std::ifstream in(dir_ / name);
        std::ostringstream out;
        out << in.rdbuf();
        return out.str();
    }
    // Counts directory entries whose filename contains `needle`.
    [[nodiscard]] int countFilesContaining(const std::string& needle) const {
        int n = 0;
        for (const auto& e : fs::directory_iterator(dir_))
            if (e.path().filename().string().find(needle) != std::string::npos) ++n;
        return n;
    }
};

TEST_F(JsonSnapshotStoreTest, EmptyStoreHasNoHeadAndEmptyList) {
    auto s = store();
    auto head = s.head();
    ASSERT_TRUE(head.has_value());
    EXPECT_FALSE(head->has_value());
    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    EXPECT_TRUE(list->empty());
    auto missing = s.read("deadbeef");
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, Error::Code::NotFound);
}

TEST_F(JsonSnapshotStoreTest, RoundTripFidelity) {
    auto s = store();
    const auto payload = payloadWith("AB12");
    auto id = s.put(payload, SnapshotTrigger::Manual, {"", "before experiment"}, 1780000100);
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(id->size(), 64u);

    auto snap = s.read(*id);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->payload, payload);
    EXPECT_EQ(snap->meta.id, *id);
    EXPECT_FALSE(snap->meta.parent.has_value());
    EXPECT_EQ(snap->meta.createdAtUtc, 1780000100);
    EXPECT_EQ(snap->meta.trigger, SnapshotTrigger::Manual);
    EXPECT_EQ(snap->meta.reason.subject, "before experiment");
    EXPECT_EQ(snap->meta.health, SnapshotHealth::Ok);
    EXPECT_EQ(snap->meta.entryCount, 1u);
    EXPECT_EQ(snap->meta.modprobeFileCount, 1u);

    auto head = s.head();
    ASSERT_TRUE(head.has_value());
    EXPECT_EQ(*head, *id);
}

TEST_F(JsonSnapshotStoreTest, ChainContinuityAcrossThreeSnapshots) {
    auto s = store();
    auto id1 = s.put(payloadWith("A"), SnapshotTrigger::Auto, {"SetDeviceEnabled", "d1"}, 100);
    auto id2 = s.put(payloadWith("B"), SnapshotTrigger::Auto, {"SetDeviceEnabled", "d2"}, 200);
    auto id3 = s.put(payloadWith("C"), SnapshotTrigger::Auto, {"SetDeviceEnabled", "d3"}, 300);
    ASSERT_TRUE(id1 && id2 && id3);

    auto head = s.head();
    ASSERT_TRUE(head.has_value());
    EXPECT_EQ(*head, *id3);
    auto s3 = s.read(*id3);
    auto s2 = s.read(*id2);
    auto s1 = s.read(*id1);
    ASSERT_TRUE(s3 && s2 && s1);
    EXPECT_EQ(s3->meta.parent, *id2);
    EXPECT_EQ(s2->meta.parent, *id1);
    EXPECT_FALSE(s1->meta.parent.has_value());

    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 3u);
    EXPECT_EQ((*list)[0].id, *id3);  // newest first
    EXPECT_EQ((*list)[1].id, *id2);
    EXPECT_EQ((*list)[2].id, *id1);
}

TEST_F(JsonSnapshotStoreTest, TornWriteLeavesNoVisibleSnapshot) {
    auto s = store();
    auto id = s.put(payloadWith("A"), SnapshotTrigger::Auto, {"v", "s"}, 100);
    ASSERT_TRUE(id.has_value());
    // Simulate a crash after the tmp file was written but before rename.
    std::ofstream(dir_ / "0123456789abcdef.json.tmp") << "{ partial garbage";

    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 1u);
    EXPECT_EQ((*list)[0].id, *id);
    auto head = s.head();
    ASSERT_TRUE(head.has_value());
    EXPECT_EQ(*head, *id);
}

TEST_F(JsonSnapshotStoreTest, TamperedSnapshotQuarantinedAndListedCorrupt) {
    auto s = store();
    auto good = s.put(payloadWith("GOOD"), SnapshotTrigger::Auto, {"v", "s1"}, 100);
    auto bad = s.put(payloadWith("BAD"), SnapshotTrigger::Auto, {"v", "s2"}, 200);
    ASSERT_TRUE(good && bad);

    // Tamper the payload without updating the id.
    const std::string name = *bad + ".json";
    auto contents = fileContents(name);
    const auto pos = contents.find("BAD");
    ASSERT_NE(pos, std::string::npos);
    contents.replace(pos, 3, "EVL");
    std::ofstream(dir_ / name, std::ios::trunc) << contents;

    auto snap = s.read(*bad);
    ASSERT_FALSE(snap.has_value());
    EXPECT_EQ(snap.error().code, Error::Code::Io);
    EXPECT_NE(snap.error().message.find(".bad-"), std::string::npos);
    EXPECT_FALSE(fs::exists(dir_ / name));        // moved aside...
    EXPECT_EQ(countFilesContaining(".bad-"), 1);  // ...never deleted

    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 2u);
    EXPECT_EQ((*list)[0].health, SnapshotHealth::Corrupt);  // off-chain corrupt follows chain?
    // Re-read after quarantine still tells the corrupt story, not NotFound.
    auto again = s.read(*bad);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error().code, Error::Code::Io);
}

TEST_F(JsonSnapshotStoreTest, FutureFormatVersionListedUnsupportedAndUntouched) {
    auto s = store();
    const std::string id(64, 'f');
    std::ofstream(dir_ / (id + ".json"))
        << R"({"formatVersion": 2, "id": ")" << id
        << R"(", "parent": null, "createdAtUtc": 42, "trigger": "auto",)"
        << R"( "reason": {"verb": "v", "subject": "s"}, "payload": {"whatever": true}})";

    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 1u);
    EXPECT_EQ((*list)[0].health, SnapshotHealth::Unsupported);
    EXPECT_EQ((*list)[0].id, id);

    auto snap = s.read(id);
    ASSERT_FALSE(snap.has_value());
    EXPECT_EQ(snap.error().code, Error::Code::Unsupported);
    auto removed = s.remove(id);
    ASSERT_FALSE(removed.has_value());
    EXPECT_EQ(removed.error().code, Error::Code::Unsupported);
    EXPECT_TRUE(fs::exists(dir_ / (id + ".json")));  // MUST NOT delete or quarantine
    EXPECT_EQ(countFilesContaining(".bad-"), 0);
}

TEST_F(JsonSnapshotStoreTest, DeleteHeadMovesHeadToParent) {
    auto s = store();
    auto id1 = s.put(payloadWith("A"), SnapshotTrigger::Auto, {"v", "s1"}, 100);
    auto id2 = s.put(payloadWith("B"), SnapshotTrigger::Auto, {"v", "s2"}, 200);
    ASSERT_TRUE(id1 && id2);

    ASSERT_TRUE(s.remove(*id2).has_value());
    auto head = s.head();
    ASSERT_TRUE(head.has_value());
    EXPECT_EQ(*head, *id1);
    EXPECT_FALSE(fs::exists(dir_ / (*id2 + ".json")));

    // A subsequent create chains from the moved HEAD.
    auto id3 = s.put(payloadWith("C"), SnapshotTrigger::Auto, {"v", "s3"}, 300);
    ASSERT_TRUE(id3.has_value());
    auto s3 = s.read(*id3);
    ASSERT_TRUE(s3.has_value());
    EXPECT_EQ(s3->meta.parent, *id1);
}

TEST_F(JsonSnapshotStoreTest, DedupeUnchangedStateReusesExistingId) {
    auto s = store();
    const auto payload = payloadWith("SAME");
    auto id1 = s.put(payload, SnapshotTrigger::Auto, {"SetDeviceEnabled", "d1"}, 100);
    auto id2 = s.put(payload, SnapshotTrigger::Auto, {"LoadModule", "snd"}, 200);
    ASSERT_TRUE(id1 && id2);
    EXPECT_EQ(*id1, *id2);
    EXPECT_EQ(countFilesContaining(".json"), 1);  // one file for one state
    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    ASSERT_EQ(list->size(), 1u);
    EXPECT_EQ((*list)[0].createdAtUtc, 100);  // original metadata retained
}

TEST_F(JsonSnapshotStoreTest, PruneKeepsLast20AutoSnapshots) {
    auto s = store();
    std::vector<std::string> ids;
    for (int i = 0; i < 21; ++i) {
        auto id = s.put(payloadWith("auto-" + std::to_string(i)), SnapshotTrigger::Auto,
                        {"SetDeviceEnabled", "d"}, 1000 + i);
        ASSERT_TRUE(id.has_value());
        ids.push_back(*id);
    }
    EXPECT_FALSE(fs::exists(dir_ / (ids.front() + ".json")));  // oldest pruned
    EXPECT_TRUE(fs::exists(dir_ / (ids[1] + ".json")));
    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->size(), devmgr::daemon::kKeepAutoSnapshots);
    EXPECT_EQ((*list)[0].id, ids.back());
    // Oldest survivor keeps its now-dangling parent id; list tolerates it.
    EXPECT_EQ((*list)[19].id, ids[1]);
    EXPECT_EQ((*list)[19].parent, ids.front());
}

TEST_F(JsonSnapshotStoreTest, ManualSnapshotsSurvivePruning) {
    auto s = store();
    auto manual = s.put(payloadWith("keep-me"), SnapshotTrigger::Manual, {"", "precious"}, 10);
    ASSERT_TRUE(manual.has_value());
    for (int i = 0; i < 21; ++i) {
        ASSERT_TRUE(s.put(payloadWith("auto-" + std::to_string(i)), SnapshotTrigger::Auto,
                          {"SetDeviceEnabled", "d"}, 1000 + i)
                        .has_value());
    }
    EXPECT_TRUE(fs::exists(dir_ / (*manual + ".json")));
    auto list = s.list();
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(list->size(), devmgr::daemon::kKeepAutoSnapshots + 1);  // 20 autos + the manual
    auto snap = s.read(*manual);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->meta.trigger, SnapshotTrigger::Manual);
}

TEST_F(JsonSnapshotStoreTest, DeleteLastSnapshotEmptiesHead) {
    auto s = store();
    auto id = s.put(payloadWith("A"), SnapshotTrigger::Manual, {"", "only"}, 100);
    ASSERT_TRUE(id.has_value());
    ASSERT_TRUE(s.remove(*id).has_value());
    auto head = s.head();
    ASSERT_TRUE(head.has_value());
    EXPECT_FALSE(head->has_value());
    auto removedTwice = s.remove(*id);
    ASSERT_FALSE(removedTwice.has_value());
    EXPECT_EQ(removedTwice.error().code, Error::Code::NotFound);
}
