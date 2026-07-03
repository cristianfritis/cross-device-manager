#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

using namespace devmgr;

namespace {
core::Device dev(std::string id, core::BusType bus, std::string name) {
    core::Device d;
    d.id = core::DeviceId{std::move(id)};
    d.bus = bus;
    d.name = std::move(name);
    d.status = core::DeviceStatus::Active;
    return d;
}

// Same shape as tests/unit/test_device_list_vm.cpp's Fixture, but with the
// real QtUiDispatcher: cross-thread posts (refresh on the scheduler) are
// delivered by processEvents() on this thread; same-thread publishes
// (applyDelta below) run the rebuild synchronously via auto-connection.
struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc};
    app::DeviceListVM vm{facade, bus, dispatcher};

    void refreshAndPump() {
        facade.refresh().wait();            // publish happened → rebuild queued
        QCoreApplication::processEvents();  // deliver it on this (GUI) thread
    }
};
}  // namespace

TEST(DeviceListModelTest, InvariantsHoldAcrossRefreshFilterAndDelta) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "GPU"));
    gui::DeviceListModel model(f.vm);
    // Fatal => any QAbstractItemModel contract violation aborts the test run.
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

    f.refreshAndPump();
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));

    f.vm.setFilter("mouse");  // direct rebuild on this thread, reset-bracketed
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));

    f.svc.applyDelta(pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added,
                                       .device = dev("u2", core::BusType::Usb, "Keyboard")});
    f.vm.setFilter("");  // clear the filter so both devices are visible again
    EXPECT_EQ(model.rowCount(), static_cast<int>(f.vm.rowsRef().size()));
}

TEST(DeviceListModelTest, ResetSignalsBracketEveryRebuild) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    gui::DeviceListModel model(f.vm);
    QSignalSpy aboutSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    f.refreshAndPump();
    EXPECT_EQ(aboutSpy.count(), 1);
    EXPECT_EQ(resetSpy.count(), 1);

    f.vm.setFilter("nomatch");
    EXPECT_EQ(aboutSpy.count(), 2);
    EXPECT_EQ(resetSpy.count(), 2);
}

TEST(DeviceListModelTest, HeaderRowsAreDisabledDeviceRowsSelectable) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    gui::DeviceListModel model(f.vm);
    f.refreshAndPump();

    ASSERT_EQ(model.rowCount(), 2);  // header + device
    ASSERT_TRUE(f.vm.isHeader(0));
    EXPECT_EQ(model.flags(model.index(0, 0)), Qt::NoItemFlags);
    EXPECT_TRUE(model.flags(model.index(1, 0)).testFlag(Qt::ItemIsSelectable));
    EXPECT_TRUE(model.flags(model.index(1, 0)).testFlag(Qt::ItemIsEnabled));
}

TEST(DeviceListModelTest, DataMirrorsVmRows) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "GPU"));
    gui::DeviceListModel model(f.vm);
    f.refreshAndPump();

    for (int i = 0; i < model.rowCount(); ++i) {
        EXPECT_EQ(model.data(model.index(i, 0), Qt::DisplayRole).toString().toStdString(),
                  f.vm.rowsRef()[static_cast<std::size_t>(i)]);
    }
    EXPECT_FALSE(model.data(model.index(0, 0), Qt::DecorationRole).isValid());  // only DisplayRole
}
