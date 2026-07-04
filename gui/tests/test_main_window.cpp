#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QAction>
#include <QCoreApplication>
#include <QLineEdit>
#include <QListView>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_pal.hpp"
#include "gui/src/main_window.hpp"
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

struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    runtime::DelayedScheduler delayed;
    test::FakePal pal;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc};
    app::DeviceListVM listVm{facade, bus, dispatcher};
    app::DeviceDetailVM detailVm{facade};
    app::StatusLineVM statusVm{bus, delayed, dispatcher};
    int refreshCalls = 0;
    std::vector<std::pair<std::string, bool>> setEnabledCalls;
    bool confirmAnswer = true;

    gui::MainWindow makeWindow() {
        return gui::MainWindow(
            facade, listVm, detailVm, statusVm, dispatcher, [this] { ++refreshCalls; },
            [this](const core::DeviceId& id, bool enable) {
                setEnabledCalls.emplace_back(id.value, enable);
            },
            [this](const QString&) { return confirmAnswer; });
    }
    void refreshAndPump() {
        facade.refresh().wait();
        QCoreApplication::processEvents();
    }
    // First selectable (non-header) row, or -1.
    int firstDeviceRow() const {
        for (int i = 0; std::cmp_less(i, listVm.rowsRef().size()); ++i)
            if (!listVm.isHeader(i)) return i;
        return -1;
    }
};
}  // namespace

TEST(MainWindowTest, SelectionFillsDetailPaneWithKeyValueRows) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();

    const int row = f.firstDeviceRow();
    ASSERT_GE(row, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(row, 0));

    auto* tree = window.detailTree();
    ASSERT_GE(tree->topLevelItemCount(), 2);
    // First detail line is "Name:    <name>" → split into ("Name", "Mouse").
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("Name"));
    EXPECT_EQ(tree->topLevelItem(0)->text(1), QStringLiteral("Mouse"));
}

TEST(MainWindowTest, SelectionSurvivesModelResetByDeviceId) {
    Fixture f;
    f.pal.seedDevice(dev("dev-beta", core::BusType::Usb, "Beta"));
    auto window = f.makeWindow();
    f.refreshAndPump();

    const int betaRow = f.firstDeviceRow();
    ASSERT_GE(betaRow, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(betaRow, 0));

    // "Alpha" sorts before "Beta" inside the USB group → Beta's row index shifts.
    f.svc.applyDelta(pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added,
                                       .device = dev("dev-alpha", core::BusType::Usb, "Alpha")});

    // The VM re-resolved selection by DeviceId; the view must follow it.
    ASSERT_TRUE(f.listVm.selectedDeviceId().has_value());
    EXPECT_EQ(f.listVm.selectedDeviceId()->value, "dev-beta");
    EXPECT_EQ(window.listView()->currentIndex().row(), f.listVm.selectedRef());
    EXPECT_EQ(window.detailTree()->topLevelItem(0)->text(1), QStringLiteral("Beta"));
}

TEST(MainWindowTest, FilterEditDrivesVmAndModel) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Logitech Mouse"));
    f.pal.seedDevice(dev("p1", core::BusType::Pci, "NVIDIA GPU"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    const int allRows = static_cast<int>(f.listVm.rowsRef().size());

    window.filterEdit()->setText(QStringLiteral("mouse"));  // fires textChanged

    EXPECT_LT(static_cast<int>(f.listVm.rowsRef().size()), allRows);
    EXPECT_EQ(window.listView()->model()->rowCount(), static_cast<int>(f.listVm.rowsRef().size()));
}

TEST(MainWindowTest, RefreshActionInvokesInjectedCallback) {
    Fixture f;
    auto window = f.makeWindow();
    auto actions = window.findChildren<QToolBar*>().first()->actions();
    ASSERT_FALSE(actions.isEmpty());
    actions.first()->trigger();
    EXPECT_EQ(f.refreshCalls, 1);
}

TEST(MainWindowTest, ToggleActionDisabledWithoutSelectionEnabledOnDeviceRow) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    EXPECT_FALSE(window.toggleAction()->isEnabled());

    f.refreshAndPump();
    const int row = f.firstDeviceRow();
    ASSERT_GE(row, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(row, 0));
    EXPECT_TRUE(window.toggleAction()->isEnabled());
    EXPECT_EQ(window.toggleAction()->text(), QStringLiteral("Disable"));
}

TEST(MainWindowTest, ConfirmedTriggerInvokesOnSetEnabled) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    window.listView()->setCurrentIndex(window.listView()->model()->index(f.firstDeviceRow(), 0));

    window.toggleAction()->trigger();
    ASSERT_EQ(f.setEnabledCalls.size(), 1u);
    EXPECT_EQ(f.setEnabledCalls[0].first, "u1");
    EXPECT_FALSE(f.setEnabledCalls[0].second);  // Active device → disable
}

TEST(MainWindowTest, DeclinedConfirmSendsNothing) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    window.listView()->setCurrentIndex(window.listView()->model()->index(f.firstDeviceRow(), 0));

    f.confirmAnswer = false;
    window.toggleAction()->trigger();
    EXPECT_TRUE(f.setEnabledCalls.empty());
}

TEST(MainWindowTest, DisabledDeviceOffersEnableWithoutGuardCheck) {
    Fixture f;
    auto d = dev("u1", core::BusType::Usb, "Webcam");
    d.status = core::DeviceStatus::Disabled;
    f.pal.seedDevice(d);
    auto window = f.makeWindow();
    f.refreshAndPump();
    window.listView()->setCurrentIndex(window.listView()->model()->index(f.firstDeviceRow(), 0));

    EXPECT_EQ(window.toggleAction()->text(), QStringLiteral("Enable"));
    window.toggleAction()->trigger();
    ASSERT_EQ(f.setEnabledCalls.size(), 1u);
    EXPECT_TRUE(f.setEnabledCalls[0].second);
}

TEST(MainWindowTest, StatusBarShowsTransientHotplugMessage) {
    Fixture f;
    auto window = f.makeWindow();
    f.statusVm.arm();  // as the composition root does after the initial refresh

    f.svc.applyDelta(pal::HotplugEvent{.action = pal::HotplugEvent::Action::Added,
                                       .device = dev("u9", core::BusType::Usb, "Webcam")});

    // Same-thread publish → StatusLineVM::setMessage → dispatcher wake runs
    // directly → taskExecuted → status bar updated, no pumping needed.
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("added")));
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("Webcam")));
}
