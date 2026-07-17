#include <gtest/gtest.h>

#include "devmgr/core/snapshot_models.hpp"

using namespace devmgr::core;

TEST(SnapshotModels, TriggerToStringUsesSpecLowercaseForms) {
    EXPECT_STREQ(to_string(SnapshotTrigger::Manual), "manual");
    EXPECT_STREQ(to_string(SnapshotTrigger::Auto), "auto");
}

TEST(SnapshotModels, TriggerFromStringRoundTripsAndRejectsUnknown) {
    EXPECT_EQ(snapshotTriggerFrom("manual"), SnapshotTrigger::Manual);
    EXPECT_EQ(snapshotTriggerFrom("auto"), SnapshotTrigger::Auto);
    EXPECT_EQ(snapshotTriggerFrom("Manual"), std::nullopt);
    EXPECT_EQ(snapshotTriggerFrom(""), std::nullopt);
    EXPECT_EQ(snapshotTriggerFrom("automatic"), std::nullopt);
}

TEST(SnapshotModels, HealthToString) {
    EXPECT_STREQ(to_string(SnapshotHealth::Ok), "ok");
    EXPECT_STREQ(to_string(SnapshotHealth::Corrupt), "corrupt");
    EXPECT_STREQ(to_string(SnapshotHealth::Unsupported), "unsupported");
}

TEST(SnapshotModels, PayloadEqualityComparesEntriesAndModprobeFiles) {
    SnapshotPayload a;
    a.entries.push_back({{"usb", "1d6b", "0002", "SER1", "2-1"},
                         "authorized",
                         "usbhid",
                         "/sys/bus/usb/devices/2-1",
                         1700000000,
                         false});
    a.modprobeFiles["devmgr-blacklist.conf"] = "blacklist pcspkr\n";
    SnapshotPayload b = a;
    EXPECT_EQ(a, b);
    b.modprobeFiles["devmgr-blacklist.conf"] = "blacklist snd\n";
    EXPECT_FALSE(a == b);
    b = a;
    b.entries.clear();
    EXPECT_FALSE(a == b);
}

TEST(SnapshotModels, MetaDefaultsAreAutoOkAndUnparented) {
    const SnapshotMeta meta;
    EXPECT_EQ(meta.trigger, SnapshotTrigger::Auto);
    EXPECT_EQ(meta.health, SnapshotHealth::Ok);
    EXPECT_FALSE(meta.parent.has_value());
    EXPECT_EQ(meta.createdAtUtc, 0);
    EXPECT_EQ(meta.entryCount, 0u);
    EXPECT_EQ(meta.modprobeFileCount, 0u);
}

TEST(SnapshotModels, ReasonSemanticsManualVsAuto) {
    // Auto: verb + subject from the triggering IPC verb.
    const SnapshotReason autoReason{"SetDeviceEnabled", "/sys/bus/usb/devices/2-1"};
    // Manual: verb empty, subject = user label (possibly empty).
    const SnapshotReason manualReason{"", "before experiment"};
    EXPECT_FALSE(autoReason.verb.empty());
    EXPECT_TRUE(manualReason.verb.empty());
    EXPECT_FALSE(autoReason == manualReason);
}
