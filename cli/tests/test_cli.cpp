#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "cli/src/cli.hpp"
#include "devmgr/core/device_detail_fields.hpp"
#include "devmgr/core/models.hpp"
#include "devmgr/core/result.hpp"
#include "devmgr/core/snapshot_diff.hpp"
#include "devmgr/core/snapshot_models.hpp"

using devmgr::core::Error;
using devmgr::core::RestoreOutcome;
using devmgr::core::SnapshotMeta;
using devmgr::core::SnapshotTrigger;
namespace cli = devmgr::cli;

namespace {

// 64-char hex-ish id from a prefix, padded so the display/short-id paths see a
// realistic shape while tests keep full control of the prefix.
std::string id64(std::string prefix, char fill = '0') {
    prefix.resize(64, fill);
    return prefix;
}

SnapshotMeta meta(std::string id, SnapshotTrigger trigger, std::string verb, std::string subject) {
    SnapshotMeta m;
    m.id = std::move(id);
    m.trigger = trigger;
    m.reason.verb = std::move(verb);
    m.reason.subject = std::move(subject);
    m.createdAtUtc = 1700000000;
    return m;
}

// In-memory IPrivilegedChannel: serves a fixed snapshot list, records the
// mutating calls, and can be told to fail any verb with a chosen Error. Only
// the snapshot verbs are exercised; the Phase 4 device/module verbs are unused
// stubs.
class FakeChannel final : public devmgr::pal::IPrivilegedChannel {
   public:
    std::vector<SnapshotMeta> list;
    std::optional<Error> listError;
    std::optional<Error> createError;
    std::optional<Error> restoreError;
    std::optional<Error> deleteError;
    std::optional<Error> diffError;

    std::optional<std::string> createdLabel;
    std::optional<std::string> restoredId;
    std::optional<std::string> deletedId;
    RestoreOutcome outcome;
    std::optional<std::string> diffedBase;
    std::optional<std::string> diffedTarget;
    devmgr::core::SnapshotDiff diff;
    std::string createdId = id64("cafe");

    devmgr::core::Result<std::vector<SnapshotMeta>> snapshotList() override {
        if (listError) return tl::unexpected(*listError);
        return list;
    }
    devmgr::core::Result<std::string> snapshotCreate(const std::string& label) override {
        createdLabel = label;
        if (createError) return tl::unexpected(*createError);
        return createdId;
    }
    devmgr::core::Result<RestoreOutcome> snapshotRestore(const std::string& id) override {
        restoredId = id;
        if (restoreError) return tl::unexpected(*restoreError);
        outcome.snapshotId = id;
        return outcome;
    }
    devmgr::core::Result<void> snapshotDelete(const std::string& id) override {
        deletedId = id;
        if (deleteError) return tl::unexpected(*deleteError);
        return {};
    }
    devmgr::core::Result<devmgr::core::SnapshotDiff> snapshotDiff(
        const std::string& baseId, const std::string& targetId) override {
        diffedBase = baseId;
        diffedTarget = targetId;
        if (diffError) return tl::unexpected(*diffError);
        return diff;
    }

    // Unused Phase 4 verbs.
    devmgr::core::Result<void> setDeviceEnabled(const devmgr::core::Device&, bool) override {
        return {};
    }
    devmgr::core::Result<void> loadModule(const std::string&) override { return {}; }
    devmgr::core::Result<void> unloadModule(const std::string&) override { return {}; }
    devmgr::core::Result<void> bindDriver(const devmgr::core::Device&,
                                          const std::string&) override {
        return {};
    }
    devmgr::core::Result<void> unbindDriver(const devmgr::core::Device&) override { return {}; }
    devmgr::core::Result<std::vector<devmgr::core::DisabledDeviceEntry>> listDisabledDevices()
        override {
        return std::vector<devmgr::core::DisabledDeviceEntry>{};
    }
};

// Convenience: run the CLI and capture streams + exit code.
struct Run {
    int code;
    std::string out;
    std::string err;
};
Run invoke(FakeChannel& ch, const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const int code = cli::run(ch, args, out, err);
    return {code, out.str(), err.str()};
}

}  // namespace

TEST(CliUsage, NoArgsIsUsageError) {
    FakeChannel ch;
    auto r = invoke(ch, {});
    EXPECT_EQ(r.code, cli::kUsage);
    EXPECT_NE(r.err.find("usage: devmgr"), std::string::npos);
}

TEST(CliUsage, NonSnapshotGroupIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"widgets", "list"}).code, cli::kUsage);
}

TEST(CliUsage, UnknownSnapshotVerbIsUsageError) {
    FakeChannel ch;
    auto r = invoke(ch, {"snapshot", "frobnicate"});
    EXPECT_EQ(r.code, cli::kUsage);
    EXPECT_NE(r.err.find("unknown snapshot command 'frobnicate'"), std::string::npos);
}

TEST(CliList, EmptyPrintsPlaceholder) {
    FakeChannel ch;
    auto r = invoke(ch, {"snapshot", "list"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_EQ(r.out, "(no snapshots)\n");
}

TEST(CliList, RowsShowShortIdTriggerAndReason) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("aa11"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.list.push_back(meta(id64("bb22"), SnapshotTrigger::Manual, "", "before-experiment"));
    auto r = invoke(ch, {"snapshot", "list"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_NE(r.out.find(id64("aa11").substr(0, 12)), std::string::npos);
    EXPECT_NE(r.out.find("auto"), std::string::npos);
    EXPECT_NE(r.out.find("SetDeviceEnabled /sys/x"), std::string::npos);
    EXPECT_NE(r.out.find("manual"), std::string::npos);
    EXPECT_NE(r.out.find("before-experiment"), std::string::npos);
}

TEST(CliList, CorruptSnapshotIsMarked) {
    FakeChannel ch;
    auto m = meta(id64("dd44"), SnapshotTrigger::Auto, "LoadModule", "evil");
    m.health = devmgr::core::SnapshotHealth::Corrupt;
    ch.list.push_back(m);
    auto r = invoke(ch, {"snapshot", "list"});
    EXPECT_NE(r.out.find("(corrupt)"), std::string::npos);
}

TEST(CliList, JsonEmitsRawMetadata) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("aa11"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "list", "--json"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_NE(r.out.find(id64("aa11")), std::string::npos);  // full id, not the short form
    EXPECT_NE(r.out.find("\"trigger\":\"auto\""), std::string::npos);
}

TEST(CliList, UnknownArgumentIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"snapshot", "list", "--wat"}).code, cli::kUsage);
}

TEST(CliCreate, NoLabelSendsEmptyLabelAndPrintsId) {
    FakeChannel ch;
    auto r = invoke(ch, {"snapshot", "create"});
    EXPECT_EQ(r.code, cli::kOk);
    ASSERT_TRUE(ch.createdLabel.has_value());
    EXPECT_EQ(*ch.createdLabel, "");  // TUI parity: unlabeled manual snapshot allowed
    EXPECT_EQ(r.out, ch.createdId + "\n");
}

TEST(CliCreate, LabelIsForwarded) {
    FakeChannel ch;
    auto r = invoke(ch, {"snapshot", "create", "--label", "pre-flash"});
    EXPECT_EQ(r.code, cli::kOk);
    ASSERT_TRUE(ch.createdLabel.has_value());
    EXPECT_EQ(*ch.createdLabel, "pre-flash");
}

TEST(CliCreate, LabelWithoutValueIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"snapshot", "create", "--label"}).code, cli::kUsage);
}

TEST(CliCreate, PermissionDeniedMapsToExitThree) {
    FakeChannel ch;
    ch.createError = Error{Error::Code::Permission, "not authorized"};
    auto r = invoke(ch, {"snapshot", "create"});
    EXPECT_EQ(r.code, cli::kNotAuthorized);
    EXPECT_EQ(r.err, "devmgr: not authorized\n");  // one line, no trace
}

TEST(CliRestore, UniquePrefixResolvesToFullIdAndPrintsOutcome) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.list.push_back(meta(id64("beef1"), SnapshotTrigger::Manual, "", "keep"));
    ch.outcome.safetySnapshotId = id64("5afe");
    ch.outcome.items.push_back(
        {.subject = "/sys/x", .action = "re-enable", .status = "ok", .detail = ""});
    auto r = invoke(ch, {"snapshot", "restore", "dead"});  // unique prefix
    EXPECT_EQ(r.code, cli::kOk);
    ASSERT_TRUE(ch.restoredId.has_value());
    EXPECT_EQ(*ch.restoredId, id64("dead0"));  // resolved to the full id
    EXPECT_NE(r.out.find("restored " + id64("dead0").substr(0, 12)), std::string::npos);
    EXPECT_NE(r.out.find("ok  re-enable  /sys/x"), std::string::npos);
}

TEST(CliRestore, AmbiguousPrefixListsMatchesAndChangesNothing) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.list.push_back(meta(id64("dead1"), SnapshotTrigger::Manual, "", "keep"));
    auto r = invoke(ch, {"snapshot", "restore", "dead"});  // matches both
    EXPECT_EQ(r.code, cli::kNotFound);
    EXPECT_NE(r.err.find("ambiguous"), std::string::npos);
    EXPECT_NE(r.err.find(id64("dead0")), std::string::npos);
    EXPECT_NE(r.err.find(id64("dead1")), std::string::npos);
    EXPECT_FALSE(ch.restoredId.has_value());  // no mutation on ambiguity
}

TEST(CliRestore, NoMatchIsNotFoundAndChangesNothing) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "restore", "ffff"});
    EXPECT_EQ(r.code, cli::kNotFound);
    EXPECT_NE(r.err.find("no snapshot matches id 'ffff'"), std::string::npos);
    EXPECT_FALSE(ch.restoredId.has_value());
}

TEST(CliRestore, MissingIdIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"snapshot", "restore"}).code, cli::kUsage);
}

TEST(CliRestore, GuardRefusedItemStillSucceeds) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.outcome.safetySnapshotId = id64("5afe");
    ch.outcome.items.push_back({.subject = "/sys/x",
                                .action = "re-apply-disable",
                                .status = "guard-refused",
                                .detail = "backs the root filesystem"});
    auto r = invoke(ch, {"snapshot", "restore", "dead0"});
    EXPECT_EQ(r.code, cli::kOk);  // partial convergence is success, reported per item
    EXPECT_NE(r.out.find("guard-refused  re-apply-disable  /sys/x — backs the root filesystem"),
              std::string::npos);
}

TEST(CliRestore, DaemonNotFoundMapsToExitTwo) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.restoreError = Error{Error::Code::NotFound, "no such snapshot"};
    auto r = invoke(ch, {"snapshot", "restore", "dead0"});
    EXPECT_EQ(r.code, cli::kNotFound);
}

TEST(CliDelete, UniquePrefixResolvesAndConfirms) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "delete", "dead0"});
    EXPECT_EQ(r.code, cli::kOk);
    ASSERT_TRUE(ch.deletedId.has_value());
    EXPECT_EQ(*ch.deletedId, id64("dead0"));
    EXPECT_EQ(r.out, "deleted " + id64("dead0").substr(0, 12) + "\n");
}

TEST(CliDelete, AmbiguousPrefixChangesNothing) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.list.push_back(meta(id64("dead1"), SnapshotTrigger::Manual, "", "keep"));
    auto r = invoke(ch, {"snapshot", "delete", "dead"});
    EXPECT_EQ(r.code, cli::kNotFound);
    EXPECT_FALSE(ch.deletedId.has_value());
}

TEST(CliErrors, DaemonUnreachableMapsToExitFour) {
    FakeChannel ch;
    // The exact text coreErrorFor() emits for a bus with no devmgrd.
    ch.listError = Error{Error::Code::Io, "helper devmgrd is not available"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kUnreachable);
    // Every verb lists (or calls) first, so all surface exit 4 when down.
    EXPECT_EQ(invoke(ch, {"snapshot", "restore", "dead0"}).code, cli::kUnreachable);
}

TEST(CliErrors, TimeoutMapsToExitFour) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Busy, "helper timed out"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kUnreachable);
}

TEST(CliErrors, GenericIoFailureMapsToExitFive) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Io, "snapshots dir is not writable"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kFailed);
}

// Daemon-unavailability arriving as an Io error message (a systemd/D-Bus name
// embedded in the text, or an activation/connection failure) must classify as
// unreachable (exit 4), not a reached-but-failed op (exit 5) — Scenario 8's
// masked-daemon case is the "Unit ... is masked." row below. Matching is a
// case-insensitive substring, so wording/capitalization drift is tolerated.
class CliUnreachableMessages : public testing::TestWithParam<std::string> {};

TEST_P(CliUnreachableMessages, MapToExitFour) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Io, GetParam()};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kUnreachable) << GetParam();
}

INSTANTIATE_TEST_SUITE_P(
    DaemonDown, CliUnreachableMessages,
    testing::Values(
        // The marker coreErrorFor() emits for a bus with no devmgrd.
        std::string{"helper devmgrd is not available"},
        // systemd refused activation because the unit is masked (Scenario 8).
        std::string{"Unit devmgrd.service is masked."},
        // Same, but with the stable systemd error name prefixed to the message.
        std::string{"org.freedesktop.systemd1.UnitMasked: Unit devmgrd.service is masked."},
        // D-Bus has no .service file / no owner for the bus name.
        std::string{"The name org.devmgr.Manager1 was not provided by any .service files"},
        // The system bus socket itself is absent (daemon and bus both down).
        std::string{"Failed to connect to socket /run/dbus/system_bus_socket: No such file or "
                    "directory"},
        // Case-insensitivity: an upper-cased phrase still classifies as down.
        std::string{"UNIT DEVMGRD.SERVICE IS MASKED."}));

// The counterpart: Io failures from a reached daemon (or an unrelated local I/O
// error) stay exit 5. "No such file or directory" in isolation — not about the
// bus socket — must NOT be mistaken for an unreachable bus.
class CliFailedMessages : public testing::TestWithParam<std::string> {};

TEST_P(CliFailedMessages, MapToExitFive) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Io, GetParam()};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kFailed) << GetParam();
}

INSTANTIATE_TEST_SUITE_P(
    ReachedButFailed, CliFailedMessages,
    testing::Values(std::string{"random I/O failure"}, std::string{"snapshots dir is not writable"},
                    std::string{"snapshot dead0 is corrupt"},
                    std::string{"restore item failed: No such file or directory"}));

// The remaining domain codes keep their own exit code, independent of message.
TEST(CliErrors, UnsupportedApiMapsToExitFive) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Unsupported, "devmgrd too old (API 2 < 3) — restart"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kFailed);
}

TEST(CliErrors, ConflictMapsToExitFive) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.restoreError = Error{Error::Code::Conflict, "critical device"};
    EXPECT_EQ(invoke(ch, {"snapshot", "restore", "dead0"}).code, cli::kFailed);
}

TEST(CliErrors, NetworkMapsToExitFive) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Network, "provider unreachable"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kFailed);
}

TEST(CliErrors, NotFoundMapsToExitTwo) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::NotFound, "no such snapshot"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kNotFound);
}

TEST(CliErrors, PermissionMapsToExitThree) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::Permission, "polkit denied"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kNotAuthorized);
}

TEST(CliErrors, InvalidArgsMapsToExitOne) {
    FakeChannel ch;
    ch.listError = Error{Error::Code::InvalidArgs, "label too long"};
    EXPECT_EQ(invoke(ch, {"snapshot", "list"}).code, cli::kUsage);
}

// ---- history ----

TEST(CliHistory, EmptyPrintsPlaceholder) {
    FakeChannel ch;
    auto r = invoke(ch, {"snapshot", "history"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_EQ(r.out, "(no snapshots)\n");
}

TEST(CliHistory, MarksHeadLastGoodAndChainStart) {
    FakeChannel ch;
    // Newest first: the child (HEAD) then its parent (a chain start).
    auto child = meta(id64("bb22"), SnapshotTrigger::Manual, "", "newest");
    child.parent = id64("aa11");
    ch.list.push_back(child);
    ch.list.push_back(meta(id64("aa11"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "history"});
    EXPECT_EQ(r.code, cli::kOk);
    // The child tip carries HEAD + last good; the parent, whose own parent is
    // absent from the list, is a chain start.
    EXPECT_NE(r.out.find(id64("bb22").substr(0, 12)), std::string::npos);
    EXPECT_NE(r.out.find("[HEAD, last good]"), std::string::npos);
    EXPECT_NE(r.out.find("[chain start]"), std::string::npos);
}

TEST(CliHistory, JsonEmitsRawMetadata) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("aa11"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "history", "--json"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_NE(r.out.find(id64("aa11")), std::string::npos);  // full id
    EXPECT_NE(r.out.find("\"trigger\":\"auto\""), std::string::npos);
}

TEST(CliHistory, UnknownArgumentIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"snapshot", "history", "--wat"}).code, cli::kUsage);
}

// ---- diff ----

devmgr::core::SnapshotDiffEntry deviceEntry() {
    return {.kind = devmgr::core::kDiffKindDevice,
            .key = "usb 1d6b:0002 @2-1",
            .before = devmgr::core::kDiffStateEnabled,
            .after = devmgr::core::kDiffStateAbsent};
}

TEST(CliDiff, TwoSnapshotsResolveBothAndPrintEntries) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.list.push_back(meta(id64("beef1"), SnapshotTrigger::Manual, "", "keep"));
    ch.diff.entries.push_back(deviceEntry());
    auto r = invoke(ch, {"snapshot", "diff", "dead", "beef"});
    EXPECT_EQ(r.code, cli::kOk);
    ASSERT_TRUE(ch.diffedBase.has_value());
    ASSERT_TRUE(ch.diffedTarget.has_value());
    EXPECT_EQ(*ch.diffedBase, id64("dead0"));    // resolved from prefix
    EXPECT_EQ(*ch.diffedTarget, id64("beef1"));  // resolved from prefix
    EXPECT_NE(r.out.find("usb 1d6b:0002 @2-1: enabled -> absent"), std::string::npos);
}

TEST(CliDiff, OneArgDiffsAgainstLiveState) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    // No entries ⇒ identical: the explicit no-differences line, not an empty result.
    auto r = invoke(ch, {"snapshot", "diff", "dead0"});
    EXPECT_EQ(r.code, cli::kOk);
    ASSERT_TRUE(ch.diffedTarget.has_value());
    EXPECT_EQ(*ch.diffedTarget, "");  // empty target ⇒ live state
    EXPECT_NE(r.out.find("No differences."), std::string::npos);
}

TEST(CliDiff, JsonEmitsDiffShape) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.diff.entries.push_back(deviceEntry());
    auto r = invoke(ch, {"snapshot", "diff", "dead0", "--json"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_NE(r.out.find("\"kind\":\"device\""), std::string::npos);
    EXPECT_NE(r.out.find("\"differences\":true"), std::string::npos);
}

TEST(CliDiff, NoArgsIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"snapshot", "diff"}).code, cli::kUsage);
}

TEST(CliDiff, UnknownPrefixIsNotFound) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "diff", "ffff"});
    EXPECT_EQ(r.code, cli::kNotFound);
    EXPECT_FALSE(ch.diffedBase.has_value());  // never reached the daemon
}

TEST(CliDiff, CorruptSnapshotMapsToExitFive) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.diffError = Error{Error::Code::Io, "snapshot dead0 is corrupt"};
    EXPECT_EQ(invoke(ch, {"snapshot", "diff", "dead0"}).code, cli::kFailed);
}

// ---- restore --preview ----

TEST(CliRestorePreview, PrintsChangeAndNoteWithoutRestoring) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    ch.diff.entries.push_back(deviceEntry());
    auto r = invoke(ch, {"snapshot", "restore", "--preview", "dead0"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_FALSE(ch.restoredId.has_value());   // nothing restored
    ASSERT_TRUE(ch.diffedTarget.has_value());  // previewed against live state
    EXPECT_EQ(*ch.diffedTarget, "");
    EXPECT_NE(r.out.find("Restore preview for " + id64("dead0").substr(0, 12)), std::string::npos);
    EXPECT_NE(r.out.find("Differences (snapshot -> current state):"), std::string::npos);
    EXPECT_NE(r.out.find("Convergence may be partial"), std::string::npos);
}

TEST(CliRestorePreview, IdenticalSnapshotSaysNothingWouldChange) {
    FakeChannel ch;
    ch.list.push_back(meta(id64("dead0"), SnapshotTrigger::Auto, "SetDeviceEnabled", "/sys/x"));
    auto r = invoke(ch, {"snapshot", "restore", "--preview", "dead0"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_FALSE(ch.restoredId.has_value());
    EXPECT_NE(r.out.find("already matches the current state"), std::string::npos);
}

TEST(CliRestorePreview, MissingIdIsUsageError) {
    FakeChannel ch;
    EXPECT_EQ(invoke(ch, {"snapshot", "restore", "--preview"}).code, cli::kUsage);
}

// ---- Inventory verbs (design D6) -------------------------------------------

namespace {

// Counts calls so "this path made no connection attempt" is asserted against
// the seam rather than inferred from the absence of an error.
class CountingChannel final : public devmgr::pal::IPrivilegedChannel {
   public:
    int calls = 0;
    devmgr::core::Result<void> setDeviceEnabled(const devmgr::core::Device&, bool) override {
        ++calls;
        return {};
    }
    devmgr::core::Result<void> bindDriver(const devmgr::core::Device&,
                                          const std::string&) override {
        ++calls;
        return {};
    }
    devmgr::core::Result<void> unbindDriver(const devmgr::core::Device&) override {
        ++calls;
        return {};
    }
    devmgr::core::Result<void> loadModule(const std::string&) override {
        ++calls;
        return {};
    }
    devmgr::core::Result<void> unloadModule(const std::string&) override {
        ++calls;
        return {};
    }
    devmgr::core::Result<std::vector<devmgr::core::DisabledDeviceEntry>> listDisabledDevices()
        override {
        ++calls;
        return std::vector<devmgr::core::DisabledDeviceEntry>{};
    }
    devmgr::core::Result<std::vector<SnapshotMeta>> snapshotList() override {
        ++calls;
        return std::vector<SnapshotMeta>{};
    }
    devmgr::core::Result<std::string> snapshotCreate(const std::string&) override {
        ++calls;
        return std::string{};
    }
    devmgr::core::Result<devmgr::core::SnapshotDiff> snapshotDiff(const std::string&,
                                                                  const std::string&) override {
        ++calls;
        return devmgr::core::SnapshotDiff{};
    }
    devmgr::core::Result<RestoreOutcome> snapshotRestore(const std::string&) override {
        ++calls;
        return RestoreOutcome{};
    }
    devmgr::core::Result<void> snapshotDelete(const std::string&) override {
        ++calls;
        return {};
    }
};

// A scriptable enumerator: either a device list or a failure, so "the query
// failed" and "the query found nothing" can be told apart in a test the same
// way a caller must be able to tell them apart at the exit code.
class FakeEnumerator final : public devmgr::pal::IDeviceEnumerator {
   public:
    std::vector<devmgr::core::Device> devices;
    std::optional<Error> failure;
    int calls = 0;
    devmgr::core::Result<std::vector<devmgr::core::Device>> enumerate() override {
        ++calls;
        if (failure) return tl::unexpected(*failure);
        return devices;
    }
};

devmgr::core::Device mouse() {
    devmgr::core::Device d;
    d.id = devmgr::core::DeviceId{"dev-1"};
    d.bus = devmgr::core::BusType::Usb;
    d.name = "Wireless Receiver";
    d.status = devmgr::core::DeviceStatus::Active;
    d.nativeId = "/sys/devices/pci0000:00/usb1/1-2";
    d.hardwareId = "usb:v046DpC52B";
    d.vendorId = "046d";
    d.productId = "c52b";
    return d;
}

struct Inventory {
    CountingChannel channel;
    FakeEnumerator enumerator;
    bool enumerationImplemented = true;

    Run invoke(const std::vector<std::string>& args) {
        std::ostringstream out;
        std::ostringstream err;
        const cli::Context context{.channel = channel,
                                   .enumerator = enumerator,
                                   .capabilities = {.deviceEnumeration = enumerationImplemented}};
        const int code = cli::run(context, args, out, err);
        return {code, out.str(), err.str()};
    }
};

}  // namespace

// design D6: the inventory verbs reach no helper, so a dead daemon cannot stop
// a user from seeing what hardware is present.
TEST(CliDevices, ListReadsTheEnumeratorAndNeverTheChannel) {
    Inventory f;
    f.enumerator.devices = {mouse()};
    auto r = f.invoke({"devices", "list"});
    EXPECT_EQ(r.code, cli::kOk);
    EXPECT_EQ(f.channel.calls, 0);
    EXPECT_NE(r.out.find("Wireless Receiver"), std::string::npos) << r.out;
    EXPECT_NE(r.out.find("USB"), std::string::npos) << r.out;
}

// task 6.2: nothing before a verb that needs the channel may touch it.
TEST(CliDevices, UsagePathsMakeNoConnectionAttempt) {
    for (const std::vector<std::string>& args :
         {std::vector<std::string>{}, std::vector<std::string>{"nonsense"},
          std::vector<std::string>{"devices"}, std::vector<std::string>{"devices", "bogus"}}) {
        Inventory f;
        auto r = f.invoke(args);
        EXPECT_EQ(r.code, cli::kUsage);
        EXPECT_EQ(f.channel.calls, 0) << "reached the channel for a usage path";
    }
}

// task 6.6: enumeration failure is kFailed and is distinguishable from an
// empty device set. It must never print an empty list and report success.
TEST(CliDevices, EnumerationFailureIsDistinguishableFromZeroDevices) {
    Inventory failing;
    failing.enumerator.failure = Error{devmgr::core::Error::Code::Io, "udev enumerate failed"};
    auto bad = failing.invoke({"devices", "list"});
    EXPECT_EQ(bad.code, cli::kFailed);
    EXPECT_TRUE(bad.out.empty()) << bad.out;
    EXPECT_NE(bad.err.find("cannot read devices"), std::string::npos) << bad.err;

    Inventory empty;
    auto none = empty.invoke({"devices", "list"});
    EXPECT_EQ(none.code, cli::kOk);
    EXPECT_NE(none.out.find("(no devices)"), std::string::npos) << none.out;
}

// task 6.8: structured output parses, carries only device data, and repeats
// byte for byte over an unchanged device set.
TEST(CliDevices, JsonIsParseableDeviceDataAndDeterministic) {
    Inventory f;
    f.enumerator.devices = {mouse()};
    auto first = f.invoke({"devices", "list", "--json"});
    auto second = f.invoke({"devices", "list", "--json"});
    EXPECT_EQ(first.code, cli::kOk);
    EXPECT_EQ(first.out, second.out);  // byte-identical across runs

    const auto parsed = nlohmann::json::parse(first.out);
    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.size(), 1U);
    EXPECT_EQ(parsed[0]["name"], "Wireless Receiver");
    EXPECT_EQ(parsed[0]["bus"], "USB");
    // Device data only: nothing about the run, the host, or the transport.
    for (const auto& [key, value] : parsed[0].items())
        for (const char* forbidden : {"elapsed", "host", "bus_address", "daemon", "timestamp"})
            EXPECT_NE(key, forbidden) << key;
    // Diagnostics never share the channel with results.
    EXPECT_TRUE(first.err.empty()) << first.err;
}

// task 6.6: `show` with no match is kNotFound, the code the exit table already
// has for "no such object" — no new code is introduced.
TEST(CliDevices, ShowWithNoMatchExitsNotFound) {
    Inventory f;
    f.enumerator.devices = {mouse()};
    auto r = f.invoke({"devices", "show", "dev-absent"});
    EXPECT_EQ(r.code, cli::kNotFound);
    EXPECT_TRUE(r.out.empty()) << r.out;
    EXPECT_NE(r.err.find("no device matches"), std::string::npos) << r.err;

    auto missingArg = f.invoke({"devices", "show"});
    EXPECT_EQ(missingArg.code, cli::kUsage);
}

// task 5a.5: `show` emits the shared detail fields under the shared labels, in
// the shared order — and no raw platform key reaches the output.
TEST(CliDevices, ShowEmitsSharedDetailFieldsWithSharedLabels) {
    Inventory f;
    auto d = mouse();
    d.properties[std::string(
        devmgr::core::detailFieldKey(devmgr::core::DetailField::Manufacturer))] = "Logitech";
    d.properties[std::string(devmgr::core::detailFieldKey(devmgr::core::DetailField::Class))] =
        "HIDClass";
    d.properties["DEVPKEY_Device_Manufacturer"] =  // native-key-guard: allow
        "should never be rendered";
    f.enumerator.devices = {d};

    auto r = f.invoke({"devices", "show", "dev-1"});
    ASSERT_EQ(r.code, cli::kOk);
    const auto manufacturer = r.out.find("Manufacturer: Logitech");
    const auto klass = r.out.find("Class: HIDClass");
    EXPECT_NE(manufacturer, std::string::npos) << r.out;
    EXPECT_NE(klass, std::string::npos) << r.out;
    EXPECT_LT(manufacturer, klass) << "detail fields out of shared order:\n" << r.out;
    EXPECT_EQ(r.out.find("DEVPKEY"), std::string::npos) << r.out;
    EXPECT_EQ(r.out.find("should never be rendered"), std::string::npos) << r.out;
}

// task 6.9: a platform with no enumerator does not advertise the verbs and does
// not pretend to run them.
TEST(CliDevices, InventoryVerbsAreGatedOnTheEnumerationCapability) {
    Inventory f;
    f.enumerationImplemented = false;
    f.enumerator.devices = {mouse()};

    auto refused = f.invoke({"devices", "list"});
    EXPECT_EQ(refused.code, cli::kUsage);
    EXPECT_EQ(f.enumerator.calls, 0) << "called a backend the platform does not implement";
    EXPECT_NE(refused.err.find("not available on this platform"), std::string::npos) << refused.err;
    EXPECT_EQ(refused.err.find("devmgr devices"), std::string::npos)
        << "usage advertised a verb this platform refuses:\n"
        << refused.err;

    // ...and where it IS implemented, usage lists it.
    Inventory capable;
    auto listed = capable.invoke({});
    EXPECT_NE(listed.err.find("devmgr devices"), std::string::npos) << listed.err;
}
