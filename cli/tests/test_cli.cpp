#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "cli/src/cli.hpp"
#include "devmgr/core/result.hpp"
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

    std::optional<std::string> createdLabel;
    std::optional<std::string> restoredId;
    std::optional<std::string> deletedId;
    RestoreOutcome outcome;
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
