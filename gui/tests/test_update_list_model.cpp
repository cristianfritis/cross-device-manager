#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/updates_vm.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_update_provider.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"
#include "gui/src/update_list_model.hpp"

using namespace devmgr;

namespace {
// One updatable candidate with a locally resolvable release (byte-frozen row
// contract shared with UpdatesVM, T10/T11/T12).
core::UpdateCandidate candidate(std::string name, std::string current, std::string next) {
    core::UpdateCandidate c;
    c.providerId = "fake";
    c.id = "a1";
    c.displayName = std::move(name);
    c.currentVersion = std::move(current);
    c.candidateVersion = next;
    c.facts = {.updatable = true, .supported = true, .needsRebootAfterUpdate = false};
    core::ReleaseInfo r;
    r.version = std::move(next);
    r.remoteId = "vendor";
    r.checksum = "abc123";
    r.localCab = true;
    c.releases.push_back(r);
    return c;
}

// Same shape as test_module_list_model.cpp's Fixture, but for UpdatesVM: the
// REAL QtUiDispatcher queues the rebuild closure (posted from a TaskScheduler
// worker running facade.refreshUpdates()) until processEvents() delivers it on
// this (GUI) thread — the exact queuing ordering UpdatesVM's alive_-token
// contract exists for (ModulesVM i-2 fix, reused verbatim), exercised here
// through the production dispatcher rather than the unit suite's test-local
// deferring one. UpdatesVM has no setFilter/fillSignatures (unlike ModulesVM):
// the async trigger here is facade.refreshUpdates() -> UpdatesRefreshedEvent
// -> queueRebuild() -> dispatcher_.post(...).
struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    tests::FakeUpdateProvider provider;
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal,     scheduler, bus,  svc,        nullptr,
                                  nullptr, &pal,      &pal, {&provider}};
    // Declared after the dispatcher so it is destroyed first: the dtor's
    // alive_-token store and lastRefresh_ wait then run while the dispatcher
    // and scheduler are still alive — the composition root's declaration-
    // order contract, reproduced at test scope.
    app::UpdatesVM vm{facade, bus, dispatcher};
};
}  // namespace

TEST(UpdateListModelTest, RowsMirrorTheVm) {
    Fixture f;
    f.provider.enumerateResult_ =
        std::vector<core::UpdateCandidate>{candidate("Webcam", "1.2.2", "1.2.4")};
    f.facade.refreshUpdates().wait();
    gui::UpdateListModel model(f.vm);  // ctor rebuild snapshots the refreshed candidate
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_TRUE(model.data(model.index(0, 0), Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("Webcam")));
    // Byte-frozen row fidelity (T10/T11/T12): the model delivers the VM string unmodified.
    EXPECT_EQ(model.data(model.index(0, 0), Qt::DisplayRole).toString().toStdString(),
              f.vm.rowsRef()[0]);
    EXPECT_FALSE(model.data(model.index(0, 0), Qt::DecorationRole).isValid());  // only DisplayRole
}

TEST(UpdateListModelTest, InvariantsHoldAcrossQueuedRebuild) {
    Fixture f;
    f.provider.enumerateResult_ =
        std::vector<core::UpdateCandidate>{candidate("Webcam", "1.2.2", "1.2.4")};
    f.facade.refreshUpdates().wait();
    gui::UpdateListModel model(f.vm);
    // Fatal => any QAbstractItemModel contract violation aborts the test run.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    EXPECT_EQ(model.rowCount(), 1);

    f.provider.enumerateResult_ = std::vector<core::UpdateCandidate>{
        candidate("Webcam", "1.2.2", "1.2.4"), candidate("GPU", "550.100", "550.120")};
    f.facade.refreshUpdates()
        .wait();  // worker done; UpdatesRefreshedEvent -> queueRebuild() queued
    QCoreApplication::processEvents();  // deliver it → rebuild under reset brackets
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));
    EXPECT_EQ(model.rowCount(), 2);
}

TEST(UpdateListModelTest, ResetSignalsBracketQueuedRebuild) {
    Fixture f;
    f.provider.enumerateResult_ =
        std::vector<core::UpdateCandidate>{candidate("Webcam", "1.2.2", "1.2.4")};
    f.facade.refreshUpdates().wait();
    gui::UpdateListModel model(f.vm);
    QSignalSpy aboutSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    f.facade.refreshUpdates().wait();  // cross-thread post: queued, not yet delivered
    EXPECT_EQ(resetSpy.count(), 0);
    QCoreApplication::processEvents();
    EXPECT_EQ(aboutSpy.count(), 1);
    EXPECT_EQ(resetSpy.count(), 1);
}

// UpdatesVM's alive_-token contract through the PRODUCTION queuing dispatcher:
// the rebuild closure is still queued on the GUI thread when the VM dies;
// delivering it afterwards must be a safe no-op (a use-after-free before the
// ModulesVM i-2 fix this VM reuses verbatim). The dispatcher deliberately
// outlives the VM, exactly like the composition root's declaration order.
TEST(UpdateListModelTest, QueuedRebuildDeliveredAfterVmDestructionIsANoOp) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    tests::FakeUpdateProvider provider;
    gui::QtUiDispatcher dispatcher;  // outlives the VM; holds the queued post
    app::ApplicationFacade facade{pal,     scheduler, bus,  svc,        nullptr,
                                  nullptr, &pal,      &pal, {&provider}};
    provider.enumerateResult_ =
        std::vector<core::UpdateCandidate>{candidate("Webcam", "1.2.2", "1.2.4")};
    QSignalSpy executed(&dispatcher, &gui::QtUiDispatcher::taskExecuted);
    {
        app::UpdatesVM vm(facade, bus, dispatcher);
        gui::UpdateListModel model(vm);
        facade.refreshUpdates().wait();  // rebuild closure now queued for the GUI thread
    }  // model unhooks first, then the VM dtor stores alive_ = false
    QCoreApplication::processEvents();  // deliver the orphaned closure
    EXPECT_GE(executed.count(), 1);     // it ran (as a no-op) — dropped work, no crash
}

// Repeated construction/teardown under load (the T12 analogue of T11's 50
// stress runs): every iteration leaves an update refresh in flight on the
// shared scheduler at destruction time — sometimes half-delivered, sometimes
// fully queued — so the dtor's alive_-store + lastRefresh_ wait and the
// model's hook unregistration are exercised across many interleavings, not
// one. UpdatesVM has no setFilter, so the refresh is driven through
// UpdatesChangedEvent (which the VM's own queueRefresh() tracks in
// lastRefresh_ — the future-custody member its destructor waits) rather than
// calling facade.refreshUpdates() directly (untracked by the VM).
TEST(UpdateListModelTest, TeardownStressUnderQueuedDispatcherLoad) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;  // default pool: max(2, hw) workers
    test::FakePal pal;
    app::DeviceService svc{bus};
    tests::FakeUpdateProvider provider;
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal,     scheduler, bus,  svc,        nullptr,
                                  nullptr, &pal,      &pal, {&provider}};
    std::vector<core::UpdateCandidate> many;
    for (int i = 0; i < 8; ++i) many.push_back(candidate("mod" + std::to_string(i), "1.0", "2.0"));
    provider.enumerateResult_ = many;

    for (int i = 0; i < 50; ++i) {
        app::UpdatesVM vm(facade, bus, dispatcher);
        gui::UpdateListModel model(vm);  // ctor rebuild + hooks
        // Triggers queueRefresh(): on this (GUI) thread the dispatcher post runs
        // inline, launching facade.refreshUpdates() tracked in lastRefresh_ —
        // in flight at destruction, exactly like ModuleListModelTest's
        // vm.fillSignatures().
        bus.publish(core::UpdatesChangedEvent{});
        if (i % 3 == 0) QCoreApplication::processEvents();  // sometimes deliver mid-life
        if (i % 2 == 0) vm.rebuild();  // reset-bracketed churn (no filter setter on UpdatesVM)
    }  // model unhooks; VM dtor waits lastRefresh_ (future custody) before subs unwind
    QCoreApplication::processEvents();  // orphaned rebuilds from every iteration: all no-op
}
