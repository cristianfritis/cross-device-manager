#include <gtest/gtest.h>

#include "devmgr/core/snapshot_diff.hpp"
#include "devmgr/core/snapshot_json.hpp"

using namespace devmgr::core;

namespace {

DisabledDeviceEntry usbEntry(std::string position = "2-1", std::string mechanism = "authorized") {
    DisabledDeviceEntry entry;
    entry.key = {.bus = "usb",
                 .vendorId = "1d6b",
                 .productId = "0002",
                 .serial = "",
                 .position = std::move(position)};
    entry.mechanism = std::move(mechanism);
    entry.lastSysfsPath = "/sys/bus/usb/devices/2-1";
    entry.disabledAtUtc = 1780000000;
    return entry;
}

const SnapshotDiffEntry* find(const SnapshotDiff& diff, const std::string& key) {
    for (const auto& e : diff.entries)
        if (e.key == key) return &e;
    return nullptr;
}

}  // namespace

TEST(SnapshotDiff, IdenticalPayloadsReportNoDifferences) {
    SnapshotPayload payload;
    payload.entries = {usbEntry()};
    payload.modprobeFiles = {{"devmgr-a.conf", "blacklist snd_hda_intel\n"}};

    const auto diff = diffPayloads(payload, payload);
    EXPECT_TRUE(diff.identical());
    EXPECT_TRUE(diff.entries.empty());
}

TEST(SnapshotDiff, NamesTheOneChangedDeviceAndNothingElse) {
    SnapshotPayload base;
    base.entries = {usbEntry("2-1"), usbEntry("3-4")};
    SnapshotPayload target;
    target.entries = {usbEntry("3-4")};  // 2-1 re-enabled

    const auto diff = diffPayloads(base, target);
    ASSERT_EQ(diff.entries.size(), 1u);
    const auto& entry = diff.entries.front();
    EXPECT_EQ(entry.kind, kDiffKindDevice);
    EXPECT_EQ(entry.key, "usb 1d6b:0002 @2-1");
    EXPECT_EQ(entry.before, "disabled (authorized)");
    EXPECT_EQ(entry.after, kDiffStateEnabled);
}

TEST(SnapshotDiff, DeviceDisabledInTargetOnly) {
    SnapshotPayload base;
    SnapshotPayload target;
    target.entries = {usbEntry("2-1", "unbind")};

    const auto diff = diffPayloads(base, target);
    ASSERT_EQ(diff.entries.size(), 1u);
    EXPECT_EQ(diff.entries.front().before, kDiffStateEnabled);
    EXPECT_EQ(diff.entries.front().after, "disabled (unbind)");
}

TEST(SnapshotDiff, MechanismChangeIsADifference) {
    SnapshotPayload base;
    base.entries = {usbEntry("2-1", "authorized")};
    SnapshotPayload target;
    target.entries = {usbEntry("2-1", "unbind")};

    const auto diff = diffPayloads(base, target);
    ASSERT_EQ(diff.entries.size(), 1u);
    EXPECT_EQ(diff.entries.front().before, "disabled (authorized)");
    EXPECT_EQ(diff.entries.front().after, "disabled (unbind)");
}

TEST(SnapshotDiff, BlacklistAppearsAsAModuleRow) {
    SnapshotPayload base;
    SnapshotPayload target;
    target.modprobeFiles = {{"devmgr-nouveau.conf", "# devmgr\nblacklist nouveau\n"}};

    const auto diff = diffPayloads(base, target);
    const auto* module = find(diff, "nouveau");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->kind, kDiffKindModule);
    EXPECT_EQ(module->before, kDiffStateAbsent);
    EXPECT_EQ(module->after, kDiffStateBlacklisted);
}

TEST(SnapshotDiff, BlacklistMovingBetweenFilesIsNotAModuleChange) {
    SnapshotPayload base;
    base.modprobeFiles = {{"devmgr-a.conf", "blacklist nouveau\n"}};
    SnapshotPayload target;
    target.modprobeFiles = {{"devmgr-b.conf", "blacklist nouveau\n"}};

    const auto diff = diffPayloads(base, target);
    EXPECT_EQ(find(diff, "nouveau"), nullptr);  // module state unchanged
    const auto* gone = find(diff, "devmgr-a.conf");
    const auto* added = find(diff, "devmgr-b.conf");
    ASSERT_NE(gone, nullptr);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(gone->after, kDiffStateAbsent);
    EXPECT_EQ(added->before, kDiffStateAbsent);
}

TEST(SnapshotDiff, CommentOnlyEditIsAFileRowNotAModuleRow) {
    SnapshotPayload base;
    base.modprobeFiles = {{"devmgr-a.conf", "blacklist nouveau\n"}};
    SnapshotPayload target;
    target.modprobeFiles = {{"devmgr-a.conf", "# written by devmgr\nblacklist nouveau\n"}};

    const auto diff = diffPayloads(base, target);
    ASSERT_EQ(diff.entries.size(), 1u);
    EXPECT_EQ(diff.entries.front().kind, kDiffKindModprobe);
    EXPECT_EQ(diff.entries.front().after, kDiffStateEdited);
}

TEST(SnapshotDiff, OptionsChangeShowsBothSides) {
    SnapshotPayload base;
    base.modprobeFiles = {{"devmgr-a.conf", "options iwlwifi power_save=0\n"}};
    SnapshotPayload target;
    target.modprobeFiles = {{"devmgr-a.conf", "options iwlwifi power_save=1\n"}};

    const auto diff = diffPayloads(base, target);
    const auto* module = find(diff, "iwlwifi");
    ASSERT_NE(module, nullptr);
    EXPECT_EQ(module->before, "options: power_save=0");
    EXPECT_EQ(module->after, "options: power_save=1");
    EXPECT_EQ(find(diff, "devmgr-a.conf"), nullptr);  // directive change, not a file row
}

TEST(SnapshotDiff, RowsAreGroupedAndStableAcrossRuns) {
    SnapshotPayload base;
    base.entries = {usbEntry("3-4"), usbEntry("2-1")};
    base.modprobeFiles = {{"devmgr-a.conf", "blacklist zzz\n"}};
    SnapshotPayload target;
    target.modprobeFiles = {{"devmgr-b.conf", "blacklist aaa\n"}};

    const auto first = diffPayloads(base, target);
    const auto second = diffPayloads(base, target);
    EXPECT_EQ(first, second);
    ASSERT_GE(first.entries.size(), 3u);
    EXPECT_EQ(first.entries[0].kind, kDiffKindDevice);
    EXPECT_EQ(first.entries[0].key, "usb 1d6b:0002 @2-1");  // sorted within the group
    EXPECT_EQ(first.entries[1].key, "usb 1d6b:0002 @3-4");
}

TEST(SnapshotDiff, EntryLabelFallsBackToSysfsPath) {
    DisabledDeviceEntry bare;
    bare.lastSysfsPath = "/sys/devices/platform/thing";
    EXPECT_EQ(deviceEntryLabel(bare), "/sys/devices/platform/thing");
}

TEST(SnapshotDiffJson, RoundTripsEntriesAndIds) {
    SnapshotDiff diff;
    diff.baseId = std::string(64, 'a');
    diff.targetId = "";
    diff.entries = {{.kind = kDiffKindDevice,
                     .key = "usb 1d6b:0002 @2-1",
                     .before = "disabled (authorized)",
                     .after = kDiffStateEnabled}};

    const auto text = snapshotDiffToJson(diff);
    EXPECT_NE(text.find("\"differences\":true"), std::string::npos);
    const auto parsed = snapshotDiffFromJson(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, diff);
}

TEST(SnapshotDiffJson, NoDifferencesMarkerIsExplicit) {
    SnapshotDiff diff;
    diff.baseId = std::string(64, 'a');
    diff.targetId = std::string(64, 'b');

    const auto text = snapshotDiffToJson(diff);
    EXPECT_NE(text.find("\"differences\":false"), std::string::npos);
    const auto parsed = snapshotDiffFromJson(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->identical());
}

TEST(SnapshotDiffJson, MalformedAndContradictoryDocumentsAreRefused) {
    EXPECT_FALSE(snapshotDiffFromJson("not json").has_value());
    EXPECT_FALSE(snapshotDiffFromJson("[]").has_value());
    EXPECT_FALSE(snapshotDiffFromJson(R"({"baseId":"a","targetId":""})").has_value());
    EXPECT_FALSE(
        snapshotDiffFromJson(R"({"baseId":"a","targetId":"","differences":true,"entries":[]})")
            .has_value());
}
