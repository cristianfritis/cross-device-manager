#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "gui/src/module_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

using namespace devmgr;

namespace {
core::LoadedModule mod(std::string name, long refs) {
    core::LoadedModule m;
    m.name = std::move(name);
    m.sizeBytes = 4096;
    m.refCount = refs;
    return m;
}

// Same shape as test_device_list_model.cpp's Fixture, but for ModulesVM: the
// REAL QtUiDispatcher queues the signature-fill merge closure (posted from a
// TaskScheduler worker) until processEvents() delivers it on this (GUI)
// thread — the exact queuing ordering ModulesVM's alive_-token contract
// exists for (T10 fix i-2), exercised here through the production dispatcher
// rather than the unit suite's test-local deferring one.
struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, nullptr, &pal, &pal};
    // Declared after the dispatcher so it is destroyed first: the dtor's
    // alive_-token store and sigFill_ wait then run while the dispatcher and
    // scheduler are still alive — the composition root's declaration-order
    // contract, reproduced at test scope.
    app::ModulesVM vm{facade, bus, scheduler, dispatcher};
};
}  // namespace

TEST(ModuleListModelTest, RowsMirrorTheVm) {
    Fixture f;
    f.pal.seedLoadedModule(mod("dummy", 0));
    gui::ModuleListModel model(f.vm);  // ctor rebuild snapshots the seeded module
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_TRUE(model.data(model.index(0, 0), Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("dummy")));
    // Byte-frozen row fidelity (T10): the model delivers the VM string unmodified.
    EXPECT_EQ(model.data(model.index(0, 0), Qt::DisplayRole).toString().toStdString(),
              f.vm.rowsRef()[0]);
    EXPECT_FALSE(model.data(model.index(0, 0), Qt::DecorationRole).isValid());  // only DisplayRole
}

TEST(ModuleListModelTest, InvariantsHoldAcrossFilterAndSignatureFill) {
    Fixture f;
    f.pal.seedLoadedModule(mod("dummy", 0));
    f.pal.seedLoadedModule(mod("usbhid", 2));
    gui::ModuleListModel model(f.vm);
    // Fatal => any QAbstractItemModel contract violation aborts the test run.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    EXPECT_EQ(model.rowCount(), 2);
    f.vm.setFilter("usb");  // direct rebuild on this thread, reset-bracketed
    EXPECT_EQ(model.rowCount(), 1);
    f.vm.setFilter("");

    f.vm.fillSignatures().wait();       // worker done; merge closure queued
    QCoreApplication::processEvents();  // deliver it → rebuild under reset brackets
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));
    // FakePal has no matching moduleInfo → the fill classifies the cell as "?".
    EXPECT_TRUE(
        model.data(model.index(0, 0), Qt::DisplayRole).toString().contains(QStringLiteral("?")));
}

TEST(ModuleListModelTest, ResetSignalsBracketQueuedSignatureMerge) {
    Fixture f;
    f.pal.seedLoadedModule(mod("dummy", 0));
    gui::ModuleListModel model(f.vm);
    QSignalSpy aboutSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    f.vm.fillSignatures().wait();  // cross-thread post: queued, not yet delivered
    EXPECT_EQ(resetSpy.count(), 0);
    QCoreApplication::processEvents();
    EXPECT_EQ(aboutSpy.count(), 1);
    EXPECT_EQ(resetSpy.count(), 1);
}

// T10's alive_-token contract through the PRODUCTION queuing dispatcher: the
// merge closure is still queued on the GUI thread when the VM dies; delivering
// it afterwards must be a safe no-op (a use-after-free before the i-2 fix).
// The dispatcher deliberately outlives the VM, exactly like the composition
// root's declaration order.
TEST(ModuleListModelTest, QueuedMergeDeliveredAfterVmDestructionIsANoOp) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;  // outlives the VM; holds the queued post
    app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, nullptr, &pal, &pal};
    pal.seedLoadedModule(mod("dummy", 0));
    QSignalSpy executed(&dispatcher, &gui::QtUiDispatcher::taskExecuted);
    {
        app::ModulesVM vm(facade, bus, scheduler, dispatcher);
        gui::ModuleListModel model(vm);
        vm.fillSignatures().wait();  // merge closure now queued for the GUI thread
    }  // model unhooks first, then the VM dtor stores alive_ = false
    QCoreApplication::processEvents();  // deliver the orphaned closure
    EXPECT_GE(executed.count(), 1);     // it ran (as a no-op) — dropped work, no crash
}

// Repeated construction/teardown under load (the T12 analogue of T11's 40
// stress runs): every iteration leaves a signature fill in flight on the
// shared scheduler at destruction time — sometimes half-delivered, sometimes
// fully queued — so the dtor's alive_-store + sigFill_ wait and the model's
// hook unregistration are exercised across many interleavings, not one.
TEST(ModuleListModelTest, TeardownStressUnderQueuedDispatcherLoad) {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler;  // default pool: max(2, hw) workers
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, nullptr, &pal, &pal};
    for (int i = 0; i < 8; ++i) pal.seedLoadedModule(mod("mod" + std::to_string(i), i % 3));

    for (int i = 0; i < 50; ++i) {
        app::ModulesVM vm(facade, bus, scheduler, dispatcher);
        gui::ModuleListModel model(vm);                     // ctor rebuild + hooks
        vm.fillSignatures();                                // in flight at destruction
        if (i % 3 == 0) QCoreApplication::processEvents();  // sometimes deliver mid-life
        vm.setFilter(i % 2 == 0 ? "mod" : "");              // reset-bracketed churn
    }  // model unhooks; VM dtor waits the worker; dispatcher survives them all
    QCoreApplication::processEvents();  // orphaned merges from every iteration: all no-op
}
