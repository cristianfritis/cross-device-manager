#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/snapshots_vm.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_privileged_channel.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"
#include "gui/src/snapshot_list_model.hpp"

using namespace devmgr;

namespace {
// 2020-09-13 12:26:40 UTC — a fixed instant so the byte-frozen row literals are
// deterministic (TZ pinned to UTC in the fixture, exactly like the VM unit
// suite's test_snapshots_vm.cpp).
constexpr std::int64_t kFixedInstant = 1600000000;

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

// The GUI analogue of test_update_list_model.cpp's Fixture, for SnapshotsVM:
// the REAL QtUiDispatcher queues the rebuild closure (posted from a
// TaskScheduler worker running facade.refreshSnapshots()) until processEvents()
// delivers it on this (GUI) thread — the exact queuing ordering SnapshotsVM's
// alive_-token contract exists for (ModulesVM i-2 fix, reused verbatim). The
// facade is fed a FakePrivilegedChannel that scripts the snapshot list.
struct Fixture {
    Fixture() {
        // Byte-frozen rows render local time; pin the zone so the literals are
        // deterministic on every machine (test_snapshots_vm.cpp discipline).
        setenv("TZ", "UTC", 1);
        tzset();
    }
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    test::FakePrivilegedChannel channel;
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc, &channel, nullptr, &pal, &pal, {}};
    // Declared after the dispatcher so it is destroyed first: the dtor's
    // alive_-token store and lastRefresh_ wait then run while the dispatcher
    // and scheduler are still alive — the composition root's declaration-order
    // contract, reproduced at test scope.
    app::SnapshotsVM vm{facade, bus, dispatcher};
};
}  // namespace

TEST(SnapshotListModelTest, RowsMirrorTheVmByteForByte) {
    Fixture f;
    f.channel.snapshotMetas = std::vector<core::SnapshotMeta>{autoMeta(), manualCorruptMeta()};
    f.facade.refreshSnapshots().wait();
    gui::SnapshotListModel model(f.vm);  // ctor rebuild snapshots the refreshed list
    ASSERT_EQ(model.rowCount(), 2);
    // The model delivers the VM string unmodified — the byte-frozen row contract
    // shared with the TUI (snapshot-ui spec "Byte-identical rows across UIs").
    EXPECT_EQ(model.data(model.index(0, 0), Qt::DisplayRole).toString().toStdString(),
              f.vm.rowsRef()[0]);
    EXPECT_EQ(model.data(model.index(1, 0), Qt::DisplayRole).toString().toStdString(),
              f.vm.rowsRef()[1]);
    EXPECT_FALSE(model.data(model.index(0, 0), Qt::DecorationRole).isValid());  // only DisplayRole
}

TEST(SnapshotListModelTest, InvariantsHoldAcrossQueuedRebuild) {
    Fixture f;
    f.channel.snapshotMetas = std::vector<core::SnapshotMeta>{autoMeta()};
    f.facade.refreshSnapshots().wait();
    gui::SnapshotListModel model(f.vm);
    // Fatal => any QAbstractItemModel contract violation aborts the test run.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    EXPECT_EQ(model.rowCount(), 1);

    f.channel.snapshotMetas = std::vector<core::SnapshotMeta>{autoMeta(), manualCorruptMeta()};
    f.facade.refreshSnapshots().wait();  // worker done; SnapshotsRefreshedEvent -> queueRebuild()
    QCoreApplication::processEvents();   // deliver it → rebuild under reset brackets
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));
    EXPECT_EQ(model.rowCount(), 2);
}

TEST(SnapshotListModelTest, ResetSignalsBracketQueuedRebuild) {
    Fixture f;
    f.channel.snapshotMetas = std::vector<core::SnapshotMeta>{autoMeta()};
    f.facade.refreshSnapshots().wait();
    gui::SnapshotListModel model(f.vm);
    QSignalSpy aboutSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    f.facade.refreshSnapshots().wait();  // cross-thread post: queued, not yet delivered
    EXPECT_EQ(resetSpy.count(), 0);
    QCoreApplication::processEvents();
    EXPECT_EQ(aboutSpy.count(), 1);
    EXPECT_EQ(resetSpy.count(), 1);
}

// The refusal path (task 5.2): a corrupt snapshot renders marked, refuses
// restore locally, but still permits delete — the same VM verb gating the TUI
// consumes, surfaced here through the model the GUI binds. selectedRef mirrors
// the QListView row the way MainWindow's currentChanged handler sets it.
TEST(SnapshotListModelTest, CorruptRowRefusesRestoreButAllowsDelete) {
    Fixture f;
    f.channel.snapshotMetas = std::vector<core::SnapshotMeta>{manualCorruptMeta()};
    f.facade.refreshSnapshots().wait();
    gui::SnapshotListModel model(f.vm);
    ASSERT_EQ(model.rowCount(), 1);
    f.vm.selectedRef() = 0;  // MainWindow sets this from the view's currentChanged
    EXPECT_FALSE(f.vm.selectedRestore().has_value());
    const auto del = f.vm.selectedDelete();
    ASSERT_TRUE(del.has_value());
    EXPECT_EQ(del->id, std::string(64, 'b'));
    // The row itself carries the corrupt marker the user scans.
    EXPECT_TRUE(model.data(model.index(0, 0), Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("corrupt")));
}

// The placeholder (empty) list is never actionable: restore AND delete refuse
// locally, so MainWindow's verb click publishes the guard message instead of
// invoking the facade.
TEST(SnapshotListModelTest, PlaceholderRowIsNeverActionable) {
    Fixture f;
    f.channel.snapshotMetas = std::vector<core::SnapshotMeta>{};
    f.facade.refreshSnapshots().wait();
    gui::SnapshotListModel model(f.vm);
    ASSERT_EQ(model.rowCount(), 1);
    f.vm.selectedRef() = 0;
    EXPECT_EQ(model.data(model.index(0, 0), Qt::DisplayRole).toString().toStdString(),
              "(no snapshots)");
    EXPECT_FALSE(f.vm.selectedRestore().has_value());
    EXPECT_FALSE(f.vm.selectedDelete().has_value());
}

// SnapshotsVM's alive_-token contract through the PRODUCTION queuing dispatcher:
// the rebuild closure is still queued on the GUI thread when the VM dies;
// delivering it afterwards must be a safe no-op (the ModulesVM i-2 fix this VM
// reuses verbatim). The dispatcher deliberately outlives the VM, exactly like
// the composition root's declaration order.
TEST(SnapshotListModelTest, QueuedRebuildDeliveredAfterVmDestructionIsANoOp) {
    setenv("TZ", "UTC", 1);
    tzset();
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    test::FakePrivilegedChannel channel;
    gui::QtUiDispatcher dispatcher;  // outlives the VM; holds the queued post
    app::ApplicationFacade facade{pal, scheduler, bus, svc, &channel, nullptr, &pal, &pal, {}};
    channel.snapshotMetas = std::vector<core::SnapshotMeta>{autoMeta()};
    QSignalSpy executed(&dispatcher, &gui::QtUiDispatcher::taskExecuted);
    {
        app::SnapshotsVM vm(facade, bus, dispatcher);
        gui::SnapshotListModel model(vm);
        facade.refreshSnapshots().wait();  // rebuild closure now queued for the GUI thread
    }  // model unhooks first, then the VM dtor stores alive_ = false
    QCoreApplication::processEvents();  // deliver the orphaned closure
    EXPECT_GE(executed.count(), 1);     // it ran (as a no-op) — dropped work, no crash
}
