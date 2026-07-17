#include <gtest/gtest.h>

#include "devmgr/core/snapshot_json.hpp"

using namespace devmgr::core;

TEST(SnapshotJson, ListRoundTripsAllFields) {
    SnapshotMeta first;
    first.id = std::string(64, 'a');
    first.parent = std::string(64, 'b');
    first.createdAtUtc = 1780000100;
    first.trigger = SnapshotTrigger::Manual;
    first.reason = {.verb = "", .subject = "before experiment"};
    first.entryCount = 2;
    first.modprobeFileCount = 1;
    SnapshotMeta second;
    second.id = std::string(64, 'b');
    second.trigger = SnapshotTrigger::Auto;
    second.reason = {.verb = "SetDeviceEnabled", .subject = "/sys/x"};
    second.health = SnapshotHealth::Corrupt;

    const auto text = snapshotListToJson({first, second});
    auto parsed = snapshotListFromJson(text);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 2u);
    EXPECT_EQ((*parsed)[0], first);
    EXPECT_EQ((*parsed)[1], second);
    // The spec's corrupt flag is present and derived from health.
    EXPECT_NE(text.find("\"corrupt\":false"), std::string::npos);
    EXPECT_NE(text.find("\"corrupt\":true"), std::string::npos);
}

TEST(SnapshotJson, RestoreOutcomeRoundTrips) {
    RestoreOutcome outcome;
    outcome.snapshotId = std::string(64, 'c');
    outcome.safetySnapshotId = std::string(64, 'd');
    outcome.items.push_back({.subject = "/sys/x",
                             .action = "re-apply-disable",
                             .status = "guard-refused",
                             .detail = "backs the root filesystem"});
    outcome.items.push_back(
        {.subject = "devmgr-a.conf", .action = "modprobe-write", .status = "ok", .detail = ""});

    auto parsed = restoreOutcomeFromJson(restoreOutcomeToJson(outcome));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, outcome);
}

TEST(SnapshotJson, MalformedInputIsIoNeverThrow) {
    EXPECT_EQ(snapshotListFromJson("{not json").error().code, Error::Code::Io);
    EXPECT_EQ(snapshotListFromJson("{}").error().code, Error::Code::Io);
    EXPECT_EQ(snapshotListFromJson(R"([{"id": 42}])").error().code, Error::Code::Io);
    EXPECT_EQ(restoreOutcomeFromJson("[]").error().code, Error::Code::Io);
    EXPECT_EQ(restoreOutcomeFromJson(R"({"snapshotId": "x"})").error().code, Error::Code::Io);
}
