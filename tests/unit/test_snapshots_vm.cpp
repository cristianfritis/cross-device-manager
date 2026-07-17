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
