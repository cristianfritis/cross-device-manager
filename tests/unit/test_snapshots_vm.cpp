#include <atomic>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/snapshots_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_privileged_channel.hpp"
#include "fakes/inline_ui_dispatcher.hpp"

using namespace devmgr;

namespace {

constexpr int kStormPublishes = 5000;
// 2020-09-13 12:26:40 UTC — a fixed instant for byte-frozen rows (TZ pinned
// to UTC in the fixture so local rendering is deterministic).
constexpr std::int64_t kFixedInstant = 1600000000;

// Queuing dispatcher with the frontend shape: post() returns before the
// closure runs. Mutex-guarded because EventBus handlers post from facade
// worker threads in the teardown-storm test.
class QueuingUiDispatcher final : public app::IUiDispatcher {
   public:
    void post(std::function<void()> fn) override {
        std::scoped_lock lock(m_);
        queued_.push_back(std::move(fn));
    }
    std::vector<std::function<void()>> drain() {
        std::scoped_lock lock(m_);
        return std::exchange(queued_, {});
    }

   private:
    std::mutex m_;
    std::vector<std::function<void()>> queued_;
};

core::SnapshotMeta autoMeta() {
    core::SnapshotMeta m;
    m.id = std::string(64, 'a');
    m.createdAtUtc = kFixedInstant;
    m.trigger = core::SnapshotTrigger::Auto;
    m.reason = {.verb = "DisableDevice", .subject = "/sys/devices/usb1/1-4"};
    m.entryCount = 2;
    m.modprobeFileCount = 1;
    return m;
}

core::SnapshotMeta manualCorruptMeta() {
    core::SnapshotMeta m;
    m.id = std::string(64, 'b');
    m.parent = std::string(64, 'a');
    m.createdAtUtc = 0;  // 1970-01-01 00:00 UTC
    m.trigger = core::SnapshotTrigger::Manual;
    m.reason = {.verb = "", .subject = "pre-upgrade"};
    m.health = core::SnapshotHealth::Corrupt;
    return m;
}

}  // namespace

class SnapshotsVmTest : public ::testing::Test {
   protected:
    SnapshotsVmTest() {
        // Byte-frozen rows render local time; pin the zone so the frozen
        // literals below are deterministic on every machine.
        setenv("TZ", "UTC", 1);
        tzset();
    }

    void seedAndRefresh(std::vector<core::SnapshotMeta> metas) {
        channel_.snapshotMetas = std::move(metas);
        facade_.refreshSnapshots().get();
    }

    runtime::EventBus bus_;
    runtime::TaskScheduler scheduler_{2};
    test::FakePal pal_;
    app::DeviceService svc_{bus_};
    test::FakePrivilegedChannel channel_;
    test::InlineUiDispatcher dispatcher_;
    app::ApplicationFacade facade_{pal_, scheduler_, bus_, svc_, &channel_};
    // SnapshotsVM holds references + Subscriptions: construct in place per test.
};

TEST_F(SnapshotsVmTest, RowsAreByteFrozenFormat) {
    seedAndRefresh({autoMeta(), manualCorruptMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 2U);
    // These literals ARE the parity contract both UIs render (V3) — byte-frozen.
    EXPECT_EQ(vm.rowsRef()[0],
              "aaaaaaaaaaaa 2020-09-13 12:26 auto   DisableDevice /sys/devices/usb ");
    EXPECT_EQ(vm.rowsRef()[1],
              "bbbbbbbbbbbb 1970-01-01 00:00 manual pre-upgrade                    corrupt");
}

TEST_F(SnapshotsVmTest, PlaceholderRowWhenEmptyNeverActionable) {
    seedAndRefresh({});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_EQ(vm.rowsRef()[0], "(no snapshots)");
    EXPECT_FALSE(vm.selectedRestore().has_value());
    EXPECT_FALSE(vm.selectedDelete().has_value());
    EXPECT_EQ(vm.banner(), "no snapshots");
    EXPECT_EQ(vm.detailLines(), std::vector<std::string>{"(no snapshot selected)"});
}

TEST_F(SnapshotsVmTest, CorruptSnapshotRefusesRestoreLocallyButAllowsDelete) {
    seedAndRefresh({manualCorruptMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    EXPECT_FALSE(vm.selectedRestore().has_value());
    const auto del = vm.selectedDelete();
    ASSERT_TRUE(del.has_value());
    EXPECT_EQ(del->id, std::string(64, 'b'));
    EXPECT_NE(del->confirmText.find("cannot be undone"), std::string::npos);
    const auto detail = vm.detailLines();
    EXPECT_NE(detail.back().find("corrupt"), std::string::npos);
}

TEST_F(SnapshotsVmTest, SelectedRestoreCarriesIdAndOutcomeSemanticsConfirmText) {
    seedAndRefresh({autoMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    const auto args = vm.selectedRestore();
    ASSERT_TRUE(args.has_value());
    EXPECT_EQ(args->id, std::string(64, 'a'));
    EXPECT_NE(args->confirmText.find("safety snapshot"), std::string::npos);
    EXPECT_NE(args->confirmText.find("guard may refuse"), std::string::npos);
}

TEST_F(SnapshotsVmTest, DetailLinesShowFullIdParentAndPayloadCounts) {
    seedAndRefresh({autoMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    const auto lines = vm.detailLines();
    ASSERT_GE(lines.size(), 6U);
    EXPECT_EQ(lines[0], "Id:      " + std::string(64, 'a'));
    EXPECT_EQ(lines[1], "Parent:  (none)");
    EXPECT_EQ(lines[2], "Created: 2020-09-13 12:26");
    EXPECT_EQ(lines[3], "Trigger: auto");
    EXPECT_EQ(lines[4], "Reason:  DisableDevice /sys/devices/usb1/1-4");
    EXPECT_EQ(lines[5], "Payload: 2 entries, 1 modprobe files");
}

TEST_F(SnapshotsVmTest, BannerCountsTriggersAndUnhealthy) {
    seedAndRefresh({autoMeta(), manualCorruptMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    EXPECT_EQ(vm.banner(), "2 snapshots · 1 auto · 1 manual · 1 unhealthy");
}

TEST_F(SnapshotsVmTest, RefreshedEventCoalescesToOneRebuild) {  // ModulesVM discipline
    seedAndRefresh({autoMeta()});
    QueuingUiDispatcher queuing;
    app::SnapshotsVM vm(facade_, bus_, queuing);
    int rebuilds = 0;
    vm.setRebuildHooks({}, [&] { ++rebuilds; });
    bus_.publish(core::SnapshotsRefreshedEvent{});
    bus_.publish(core::SnapshotsRefreshedEvent{});
    bus_.publish(core::SnapshotsRefreshedEvent{});
    for (auto& fn : queuing.drain()) fn();
    EXPECT_EQ(rebuilds, 1);
    // The rebuild consumed the facade list — the cross-frontend path: any
    // frontend's mutation completion refreshes THIS view via the event, no
    // manual reload.
    EXPECT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_NE(vm.rowsRef()[0].find("aaaaaaaaaaaa"), std::string::npos);
}

TEST_F(SnapshotsVmTest, ChangedEventCoalescesToOneRefresh) {  // D6 custody + wiring
    QueuingUiDispatcher queuing;
    {
        app::SnapshotsVM vm(facade_, bus_, queuing);
        bus_.publish(core::SnapshotsChangedEvent{});
        bus_.publish(core::SnapshotsChangedEvent{});
        auto closures = queuing.drain();
        ASSERT_EQ(closures.size(), 1U);  // coalesced to ONE queued refresh
        for (auto& fn : closures) fn();  // launches facade_.refreshSnapshots()
    }  // dtor waits lastRefresh_ (future custody) → refresh completed here
    EXPECT_EQ(channel_.listCalls.load(), 1);  // channel listed exactly once
}

TEST_F(SnapshotsVmTest, TeardownStormNoPostAfterDrain) {  // review test 14 (VM level)
    seedAndRefresh({autoMeta()});
    QueuingUiDispatcher queuing;
    std::atomic<int> rebuilds{0};
    std::vector<std::function<void()>> orphans;
    {
        app::SnapshotsVM vm(facade_, bus_, queuing);
        vm.setRebuildHooks({}, [&] { ++rebuilds; });
        // Hammer from another thread while the VM is alive; join BEFORE the VM
        // dies so every post lands in the queue and outlives the VM.
        std::thread hammer([&] {
            for (int i = 0; i < kStormPublishes; ++i) bus_.publish(core::SnapshotsRefreshedEvent{});
        });
        hammer.join();
        orphans = queuing.drain();
    }  // VM destroyed here; queued closures orphaned
    ASSERT_EQ(orphans.size(), 1U);  // the storm coalesced to ONE queued rebuild
    for (auto& fn : orphans) fn();  // must not crash; must be a no-op (alive token)
    EXPECT_EQ(rebuilds.load(), 0);  // never drained while alive → never rebuilt
}

// ---- beta-06 task 3.1: filter, history chain, preview, recovery guidance ----

namespace {

// A third meta so a chain has an interior row, and a pruned-parent row that
// must render as a chain start rather than an error.
core::SnapshotMeta childMeta() {
    core::SnapshotMeta m;
    m.id = std::string(64, 'c');
    m.parent = std::string(64, 'b');
    m.createdAtUtc = kFixedInstant;
    m.trigger = core::SnapshotTrigger::Manual;
    m.reason = {.verb = "", .subject = "before nvidia swap"};
    return m;
}

core::SnapshotMeta prunedParentMeta() {
    core::SnapshotMeta m;
    m.id = std::string(64, 'd');
    m.parent = std::string(64, 'f');  // never present in the list — pruned
    m.createdAtUtc = kFixedInstant;
    m.trigger = core::SnapshotTrigger::Auto;
    m.reason = {.verb = "UnloadModule", .subject = "nouveau"};
    return m;
}

core::SnapshotDiff oneDeviceDiff() {
    core::SnapshotDiff d;
    d.baseId = std::string(64, 'a');
    d.entries.push_back({.kind = core::kDiffKindDevice,
                         .key = "usb 1d6b:0002 @2-1",
                         .before = "disabled (authorized)",
                         .after = core::kDiffStateAbsent});
    return d;
}

}  // namespace

TEST_F(SnapshotsVmTest, FilterMatchesIdTriggerAndReasonCaseInsensitively) {
    seedAndRefresh({autoMeta(), manualCorruptMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();

    vm.setFilter("AUTO");  // trigger, upper-case → matches the auto row only
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_NE(vm.rowsRef()[0].find("aaaaaaaaaaaa"), std::string::npos);

    vm.setFilter("pre-upgrade");  // reason
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_NE(vm.rowsRef()[0].find("bbbbbbbbbbbb"), std::string::npos);

    vm.setFilter("bbbb");  // id prefix
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_NE(vm.rowsRef()[0].find("bbbbbbbbbbbb"), std::string::npos);

    vm.setFilter("");
    EXPECT_EQ(vm.rowsRef().size(), 2U);
}

TEST_F(SnapshotsVmTest, FilterWithNoMatchesNamesTheFilterAndStaysNonActionable) {
    seedAndRefresh({autoMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    vm.setFilter("zzz");
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    // Names the filter (docs/DESIGN.md §5.1) and is distinct from the empty-store row.
    EXPECT_EQ(vm.rowsRef()[0], "No snapshots match \"zzz\"");
    EXPECT_FALSE(vm.selectedRestore().has_value());
    EXPECT_FALSE(vm.selectedDelete().has_value());
}

TEST_F(SnapshotsVmTest, HistoryViewMarksHeadLastGoodAndChainStartsByteFrozen) {
    // Newest first: c -> b -> a is the chain; d's parent was pruned.
    seedAndRefresh({childMeta(), manualCorruptMeta(), autoMeta(), prunedParentMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.setHistoryView(true);
    ASSERT_EQ(vm.rowsRef().size(), 4U);
    // Byte-frozen: these strings ARE what both UIs render. Indent runs from the
    // chain TIP, so the newest sits at the margin and each ancestor steps right.
    EXPECT_EQ(vm.rowsRef()[0],
              "cccccccccccc 2020-09-13 12:26 manual before nvidia swap             "
              "  [HEAD, last good]");
    // The corrupt interior row is one step in and carries no chain marker.
    EXPECT_EQ(vm.rowsRef()[1],
              "  bbbbbbbbbbbb 1970-01-01 00:00 manual pre-upgrade                    corrupt");
    // Oldest of the chain: two steps in, and its own parent is absent.
    EXPECT_EQ(vm.rowsRef()[2],
              "    aaaaaaaaaaaa 2020-09-13 12:26 auto   DisableDevice /sys/devices/usb "
              "  [chain start]");
    // Pruned parent restarts at the left margin as a chain start, not an error.
    EXPECT_EQ(vm.rowsRef()[3],
              "dddddddddddd 2020-09-13 12:26 auto   UnloadModule nouveau           "
              "  [chain start]");
}

TEST_F(SnapshotsVmTest, HistoryMarkersStayTrueWhenTheFilterHidesHead) {
    seedAndRefresh({childMeta(), manualCorruptMeta(), autoMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.setHistoryView(true);
    // Hide the real HEAD (c). The surviving rows must NOT be relabelled HEAD —
    // markers describe the store, not the visible subset (docs/DESIGN.md §2.1).
    vm.setFilter("pre-upgrade");
    ASSERT_EQ(vm.rowsRef().size(), 1U);
    EXPECT_EQ(vm.rowsRef()[0].find("[HEAD"), std::string::npos);
    EXPECT_EQ(vm.rowsRef()[0].find("last good"), std::string::npos);
}

TEST_F(SnapshotsVmTest, PreviewReportsLoadingThenTheDiffAgainstLiveState) {
    seedAndRefresh({autoMeta()});
    channel_.nextDiff = oneDeviceDiff();
    QueuingUiDispatcher queuing;
    app::SnapshotsVM vm(facade_, bus_, queuing);
    vm.rebuild();

    auto handle = vm.requestPreview(std::string(64, 'a'));
    // Before the diff lands the surface states the loading condition; it never
    // shows an empty or falsely-identical result.
    auto loading = vm.previewLines();
    EXPECT_NE(std::find(loading.begin(), loading.end(), "Computing what will change..."),
              loading.end());

    handle.wait();
    for (auto& fn : queuing.drain()) fn();  // deliver SnapshotDiffRefreshedEvent

    const auto lines = vm.previewLines();
    ASSERT_GE(lines.size(), 4U);
    EXPECT_EQ(lines[0], "Restore snapshot aaaaaaaaaaaa?");
    EXPECT_EQ(lines[1], "Selected:     aaaaaaaaaaaa (created 2020-09-13 12:26)");
    EXPECT_EQ(lines[2], "Current HEAD: aaaaaaaaaaaa — this snapshot");
    EXPECT_EQ(lines[3], "Last good:    aaaaaaaaaaaa — this snapshot");
    // The diff row, its direction labelled, and the partial-convergence note.
    const auto joined = [&lines] {
        std::string s;
        for (const auto& l : lines) s += l + "\n";
        return s;
    }();
    EXPECT_NE(joined.find("Differences (snapshot -> current state):"), std::string::npos);
    EXPECT_NE(joined.find("device   usb 1d6b:0002 @2-1: disabled (authorized) -> absent"),
              std::string::npos);
    EXPECT_NE(joined.find("Restoring re-applies the snapshot side"), std::string::npos);
    EXPECT_NE(joined.find("Convergence may be partial"), std::string::npos);
    // Live-state form: empty target id (design decision 1).
    EXPECT_NE(std::find(channel_.snapshotCalls.begin(), channel_.snapshotCalls.end(),
                        "diff:" + std::string(64, 'a') + ":live"),
              channel_.snapshotCalls.end());
}

TEST_F(SnapshotsVmTest, PreviewOfIdenticalPayloadsSaysNothingWouldChange) {
    seedAndRefresh({autoMeta()});
    channel_.nextDiff = core::SnapshotDiff{};  // no entries == identical
    QueuingUiDispatcher queuing;
    app::SnapshotsVM vm(facade_, bus_, queuing);
    vm.rebuild();
    vm.requestPreview(std::string(64, 'a')).wait();
    for (auto& fn : queuing.drain()) fn();

    const auto lines = vm.previewLines();
    const auto joined = [&lines] {
        std::string s;
        for (const auto& l : lines) s += l + "\n";
        return s;
    }();
    // Explicit "no differences", never an empty section or a failure.
    EXPECT_NE(joined.find("already matches the current state; nothing would change"),
              std::string::npos);
    EXPECT_EQ(joined.find("Differences (snapshot"), std::string::npos);
}

TEST_F(SnapshotsVmTest, FailedDiffFetchSaysUnavailableRatherThanShowingAStaleDiff) {
    seedAndRefresh({autoMeta(), manualCorruptMeta()});
    channel_.nextDiff = oneDeviceDiff();
    QueuingUiDispatcher queuing;
    app::SnapshotsVM vm(facade_, bus_, queuing);
    vm.rebuild();
    vm.requestPreview(std::string(64, 'a')).wait();
    for (auto& fn : queuing.drain()) fn();
    ASSERT_NE(vm.previewLines().size(), 0U);

    // Second preview fails: the first diff must NOT be shown for it.
    channel_.nextDiff = tl::unexpected(
        core::Error{.code = core::Error::Code::Io, .message = "snapshot payload unreadable"});
    vm.requestPreview(std::string(64, 'b')).wait();
    for (auto& fn : queuing.drain()) fn();

    const auto lines = vm.previewLines();
    const auto joined = [&lines] {
        std::string s;
        for (const auto& l : lines) s += l + "\n";
        return s;
    }();
    EXPECT_NE(joined.find("What will change is unavailable"), std::string::npos);
    EXPECT_EQ(joined.find("usb 1d6b:0002"), std::string::npos);  // no stale rows
}

TEST_F(SnapshotsVmTest, RestoreGuidanceNamesFailedItemsSafetyIdAndRecoveryCommand) {
    seedAndRefresh({autoMeta()});
    core::RestoreOutcome outcome;
    outcome.snapshotId = std::string(64, 'a');
    outcome.safetySnapshotId = std::string(64, 'e');
    outcome.items.push_back(
        {.subject = "/sys/devices/usb1/1-4", .action = "re-enable", .status = "ok", .detail = ""});
    outcome.items.push_back({.subject = "/sys/devices/usb1/1-2",
                             .action = "re-apply-disable",
                             .status = "guard-refused",
                             .detail = "only remaining keyboard"});
    channel_.nextRestore = outcome;
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    facade_.restoreSnapshot(std::string(64, 'a')).get();

    const auto lines = vm.restoreGuidanceLines();
    ASSERT_EQ(lines.size(), 4U);
    EXPECT_EQ(lines[0], "Restore of aaaaaaaaaaaa left 1 item unconverged:");
    EXPECT_EQ(lines[1],
              "  guard-refused  /sys/devices/usb1/1-2 (re-apply-disable): only remaining keyboard");
    EXPECT_EQ(lines[2],
              "The state from before this restore is kept as safety snapshot eeeeeeeeeeee.");
    // The exact command to fall back to — never a bare error with no next step.
    EXPECT_EQ(lines[3], "To go back, run: devmgr snapshot restore eeeeeeeeeeee");
}

TEST_F(SnapshotsVmTest, FullyConvergedRestoreOffersNoGuidance) {
    seedAndRefresh({autoMeta()});
    core::RestoreOutcome outcome;
    outcome.snapshotId = std::string(64, 'a');
    outcome.safetySnapshotId = std::string(64, 'e');
    outcome.items.push_back(
        {.subject = "/sys/devices/usb1/1-4", .action = "re-enable", .status = "ok", .detail = ""});
    channel_.nextRestore = outcome;
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    facade_.restoreSnapshot(std::string(64, 'a')).get();
    // Nothing to recover from → nothing to show, not an empty box.
    EXPECT_TRUE(vm.restoreGuidanceLines().empty());
}

TEST_F(SnapshotsVmTest, FailedRestoreLeavesNoStaleGuidanceFromAnEarlierOne) {
    seedAndRefresh({autoMeta()});
    core::RestoreOutcome outcome;
    outcome.snapshotId = std::string(64, 'a');
    outcome.safetySnapshotId = std::string(64, 'e');
    outcome.items.push_back({.subject = "/sys/devices/usb1/1-2",
                             .action = "re-apply-disable",
                             .status = "failed",
                             .detail = "write error"});
    channel_.nextRestore = outcome;
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    facade_.restoreSnapshot(std::string(64, 'a')).get();
    ASSERT_FALSE(vm.restoreGuidanceLines().empty());

    // A restore that fails outright has no outcome — guidance naming the OLD
    // safety snapshot would send the user to the wrong recovery point.
    channel_.nextRestore = tl::unexpected(
        core::Error{.code = core::Error::Code::NotFound, .message = "no such snapshot"});
    facade_.restoreSnapshot(std::string(64, 'b')).get();
    EXPECT_TRUE(vm.restoreGuidanceLines().empty());
}

TEST_F(SnapshotsVmTest, HealthForRowAndHeadLastGoodMarkersLandOnDifferentRows) {
    using core::SnapshotHealth;
    // Chain b(corrupt) -> a(healthy): b is the chain tip (HEAD); a is the most
    // recent healthy snapshot (last good). The two markers land on different
    // rows, which is exactly what group 4's accent colouring needs to tell apart.
    seedAndRefresh({manualCorruptMeta(), autoMeta()});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 2U);

    auto rowOf = [&](const char* idPrefix) {
        for (int i = 0; std::cmp_less(i, vm.rowsRef().size()); ++i)
            if (vm.rowsRef()[i].find(idPrefix) != std::string::npos) return i;
        return -1;
    };
    const int rowB = rowOf("bbbbbbbbbbbb");
    const int rowA = rowOf("aaaaaaaaaaaa");
    ASSERT_GE(rowB, 0);
    ASSERT_GE(rowA, 0);

    EXPECT_EQ(vm.healthForRow(rowB), SnapshotHealth::Corrupt);
    EXPECT_EQ(vm.healthForRow(rowA), SnapshotHealth::Ok);

    EXPECT_TRUE(vm.isHeadRow(rowB));       // b is the chain tip
    EXPECT_FALSE(vm.isLastGoodRow(rowB));  // ...but corrupt, so not last-good
    EXPECT_FALSE(vm.isHeadRow(rowA));
    EXPECT_TRUE(vm.isLastGoodRow(rowA));  // a is the most recent healthy snapshot

    // Markers are computed with the history view OFF too — colouring needs them
    // in the flat list, not only the chain view.
    EXPECT_FALSE(vm.historyView());
}

TEST_F(SnapshotsVmTest, HealthAndMarkersAreEmptyOnPlaceholderAndOutOfRange) {
    seedAndRefresh({});
    app::SnapshotsVM vm(facade_, bus_, dispatcher_);
    vm.rebuild();
    ASSERT_EQ(vm.rowsRef().size(), 1U);  // "(no snapshots)" placeholder
    EXPECT_FALSE(vm.healthForRow(0).has_value());
    EXPECT_FALSE(vm.isHeadRow(0));
    EXPECT_FALSE(vm.isLastGoodRow(0));
    EXPECT_FALSE(vm.healthForRow(-1).has_value());
    EXPECT_FALSE(vm.healthForRow(99).has_value());
    EXPECT_FALSE(vm.isHeadRow(99));
}
