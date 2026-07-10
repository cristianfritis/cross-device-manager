#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QAction>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_criticality_prober.hpp"
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
    test::FakeCriticalityProber prober;
    app::DeviceService svc{bus};
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal, scheduler, bus, svc, nullptr, &prober, &pal, &pal};
    app::DeviceListVM listVm{facade, bus, dispatcher};
    app::DeviceDetailVM detailVm{facade};
    app::StatusLineVM statusVm{bus, delayed, dispatcher};
    // Declared after the dispatcher so it is destroyed first: the ModulesVM
    // dtor's alive_-token store and sigFill_ wait must run while dispatcher
    // and scheduler are still alive — the composition root's declaration-
    // order contract, reproduced at fixture scope.
    app::ModulesVM modulesVm{facade, bus, scheduler, dispatcher};
    int refreshCalls = 0;
    int confirmCalls = 0;
    std::vector<std::pair<std::string, bool>> setEnabledCalls;
    std::vector<std::string> loadModuleCalls;
    std::vector<std::string> unloadModuleCalls;
    std::vector<std::pair<std::string, std::string>> bindDriverCalls;
    std::vector<std::string> unbindDriverCalls;
    bool confirmAnswer = true;
    QString textAnswer;       // returned by the injected textInput seam
    QString lastTextPrefill;  // captured prefill the dialog would have shown

    gui::MainWindow makeWindow() {
        gui::MainWindow::Actions actions;
        actions.onRefresh = [this] { ++refreshCalls; };
        actions.onSetEnabled = [this](const core::DeviceId& id, bool enable) {
            setEnabledCalls.emplace_back(id.value, enable);
        };
        actions.onLoadModule = [this](const std::string& name) { loadModuleCalls.push_back(name); };
        actions.onUnloadModule = [this](const std::string& name) {
            unloadModuleCalls.push_back(name);
        };
        actions.onBindDriver = [this](const core::DeviceId& id, const std::string& driver) {
            bindDriverCalls.emplace_back(id.value, driver);
        };
        actions.onUnbindDriver = [this](const core::DeviceId& id) {
            unbindDriverCalls.push_back(id.value);
        };
        actions.confirm = [this](const QString&) {
            ++confirmCalls;
            return confirmAnswer;
        };
        actions.textInput = [this](const QString&, const QString& prefill) {
            lastTextPrefill = prefill;
            return textAnswer;
        };
        return gui::MainWindow(facade, listVm, detailVm, statusVm, modulesVm, dispatcher,
                               std::move(actions));
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
    void selectFirstDevice(gui::MainWindow& window) {
        window.listView()->setCurrentIndex(window.listView()->model()->index(firstDeviceRow(), 0));
    }
    void seedModule(const std::string& name, long refs, std::vector<std::string> holders = {}) {
        core::LoadedModule m;
        m.name = name;
        m.sizeBytes = 4096;
        m.refCount = refs;
        m.holders = std::move(holders);
        pal.seedLoadedModule(m);
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
    f.selectFirstDevice(window);

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
    f.selectFirstDevice(window);

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
    f.selectFirstDevice(window);

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

// ----- T12: Modules tab + driver actions -----

TEST(MainWindowTest, ModulesTabGatesActionEnablement) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.seedModule("dummy", 0);
    auto window = f.makeWindow();
    f.refreshAndPump();

    ASSERT_EQ(window.tabs()->count(), 2);
    // Devices tab: module actions off; device actions follow the selection.
    EXPECT_FALSE(window.loadModuleAction()->isEnabled());
    EXPECT_FALSE(window.unloadModuleAction()->isEnabled());
    EXPECT_FALSE(window.unbindAction()->isEnabled());  // nothing selected yet
    EXPECT_FALSE(window.bindAction()->isEnabled());
    f.selectFirstDevice(window);
    EXPECT_TRUE(window.unbindAction()->isEnabled());
    EXPECT_TRUE(window.bindAction()->isEnabled());

    window.tabs()->setCurrentIndex(1);
    EXPECT_TRUE(window.loadModuleAction()->isEnabled());
    // The VM starts on row 0 = "dummy" → unload has a target.
    EXPECT_TRUE(window.unloadModuleAction()->isEnabled());
    EXPECT_FALSE(window.bindAction()->isEnabled());  // device actions off on Modules tab
    EXPECT_FALSE(window.unbindAction()->isEnabled());
    EXPECT_FALSE(window.toggleAction()->isEnabled());

    window.tabs()->setCurrentIndex(0);
    EXPECT_FALSE(window.loadModuleAction()->isEnabled());
    EXPECT_TRUE(window.bindAction()->isEnabled());  // device selection retained
}

TEST(MainWindowTest, PlaceholderRowDisablesUnload) {
    Fixture f;  // no modules seeded → "(no modules)" placeholder row
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    EXPECT_TRUE(window.loadModuleAction()->isEnabled());
    EXPECT_FALSE(window.unloadModuleAction()->isEnabled());
}

TEST(MainWindowTest, ModulesTabEntrySetsBannerRowsAndAsyncSignatureFill) {
    Fixture f;
    f.pal.info.secureBoot = true;
    f.pal.info.lockdownMode = "integrity";
    f.seedModule("dummy", 0);
    auto window = f.makeWindow();

    window.tabs()->setCurrentIndex(1);  // banner + rebuild + fillSignatures
    // Byte-frozen banner (T10): rendered exactly as the VM emits it.
    EXPECT_EQ(window.bannerLabel()->text(), QString::fromStdString(f.modulesVm.banner()));
    EXPECT_TRUE(window.bannerLabel()->text().contains(QStringLiteral("Secure Boot: ON")));

    auto* model = window.modulesView()->model();
    ASSERT_EQ(model->rowCount(), 1);
    const QString before = model->data(model->index(0, 0), Qt::DisplayRole).toString();
    EXPECT_TRUE(before.contains(QStringLiteral("dummy")));
    EXPECT_TRUE(before.contains(QStringLiteral("…")));  // async fill still pending

    f.modulesVm.fillSignatures().wait();  // coalesces onto the in-flight fill
    QCoreApplication::processEvents();    // deliver the merge → rebuild
    const QString after = model->data(model->index(0, 0), Qt::DisplayRole).toString();
    EXPECT_TRUE(after.contains(QStringLiteral("?")));  // unclassifiable in FakePal
    // Byte-frozen row fidelity end to end (VM → Qt model → view data).
    EXPECT_EQ(after.toStdString(), f.modulesVm.rowsRef()[0]);
}

TEST(MainWindowTest, ModuleFilterEditDrivesVmAndModel) {
    Fixture f;
    f.seedModule("dummy", 0);
    f.seedModule("usbhid", 2);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    ASSERT_EQ(window.modulesView()->model()->rowCount(), 2);

    window.moduleFilterEdit()->setText(QStringLiteral("usb"));  // fires textChanged

    ASSERT_EQ(window.modulesView()->model()->rowCount(), 1);
    EXPECT_TRUE(window.modulesView()
                    ->model()
                    ->data(window.modulesView()->model()->index(0, 0), Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("usbhid")));
}

TEST(MainWindowTest, ModuleDetailPaneShowsSelectedModule) {
    Fixture f;
    f.seedModule("dummy", 0);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    window.modulesView()->setCurrentIndex(window.modulesView()->model()->index(0, 0));

    auto* tree = window.moduleDetailTree();
    ASSERT_GE(tree->topLevelItemCount(), 1);
    // First detail line is "Module:  <name>" → split into ("Module", "dummy").
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("Module"));
    EXPECT_EQ(tree->topLevelItem(0)->text(1), QStringLiteral("dummy"));
}

TEST(MainWindowTest, LoadModuleFlowsThroughTextInputAndCallback) {
    Fixture f;
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);

    f.textAnswer = QStringLiteral("dummy");
    window.loadModuleAction()->trigger();
    ASSERT_EQ(f.loadModuleCalls.size(), 1u);
    EXPECT_EQ(f.loadModuleCalls[0], "dummy");
}

TEST(MainWindowTest, LoadModuleRejectsInvalidOrEmptyName) {
    Fixture f;
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);

    f.textAnswer = QStringLiteral("bad name!");  // fails ^[A-Za-z0-9_-]+$
    window.loadModuleAction()->trigger();
    f.textAnswer.clear();  // cancelled / empty input
    window.loadModuleAction()->trigger();
    EXPECT_TRUE(f.loadModuleCalls.empty());
}

TEST(MainWindowTest, UnloadConfirmedSendsSelectedModule) {
    Fixture f;
    f.seedModule("dummy", 0);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    window.modulesView()->setCurrentIndex(window.modulesView()->model()->index(0, 0));

    window.unloadModuleAction()->trigger();
    EXPECT_EQ(f.confirmCalls, 1);
    ASSERT_EQ(f.unloadModuleCalls.size(), 1u);
    EXPECT_EQ(f.unloadModuleCalls[0], "dummy");
}

TEST(MainWindowTest, DeclinedUnloadSendsNothing) {
    Fixture f;
    f.seedModule("dummy", 0);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    window.modulesView()->setCurrentIndex(window.modulesView()->model()->index(0, 0));

    f.confirmAnswer = false;
    window.unloadModuleAction()->trigger();
    EXPECT_TRUE(f.unloadModuleCalls.empty());
}

TEST(MainWindowTest, UnloadGuardRefusalShowsReasonWithoutConfirm) {
    Fixture f;
    f.seedModule("dummy", 0, {"holder_mod"});  // held module → guard refuses first
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    window.modulesView()->setCurrentIndex(window.modulesView()->model()->index(0, 0));

    window.unloadModuleAction()->trigger();
    EXPECT_TRUE(f.unloadModuleCalls.empty());
    EXPECT_EQ(f.confirmCalls, 0);  // refusal short-circuits before the confirm
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("cannot unload:")));
}

TEST(MainWindowTest, UnbindConfirmedInvokesCallback) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    window.unbindAction()->trigger();
    EXPECT_EQ(f.confirmCalls, 1);
    ASSERT_EQ(f.unbindDriverCalls.size(), 1u);
    EXPECT_EQ(f.unbindDriverCalls[0], "u1");
}

TEST(MainWindowTest, UnbindGuardRefusalShowsReasonWithoutConfirm) {
    Fixture f;
    auto d = dev("u1", core::BusType::Usb, "RootDisk");
    d.sysfsPath = "/sys/devices/root-disk";
    f.pal.seedDevice(d);
    f.prober.next = pal::CriticalityFacts{.rootBackingPaths = {"/sys/devices/root-disk"}};
    auto window = f.makeWindow();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    window.unbindAction()->trigger();
    EXPECT_TRUE(f.unbindDriverCalls.empty());
    EXPECT_EQ(f.confirmCalls, 0);
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("cannot unbind:")));
}

TEST(MainWindowTest, BindPrefillsBoundDriverAndInvokesCallback) {
    Fixture f;
    auto d = dev("u1", core::BusType::Usb, "Mouse");
    d.boundDriver = "usbhid";
    f.pal.seedDevice(d);
    auto window = f.makeWindow();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    f.textAnswer = QStringLiteral("usbhid");
    window.bindAction()->trigger();
    EXPECT_EQ(f.lastTextPrefill, QStringLiteral("usbhid"));
    ASSERT_EQ(f.bindDriverCalls.size(), 1u);
    EXPECT_EQ(f.bindDriverCalls[0].first, "u1");
    EXPECT_EQ(f.bindDriverCalls[0].second, "usbhid");
}

TEST(MainWindowTest, BindPrefillFallsBackToDriverCandidates) {
    Fixture f;
    auto d = dev("u1", core::BusType::Usb, "Mouse");
    d.sysfsPath = "/sys/devices/u1";
    f.pal.seedDevice(d);
    core::Driver candidate;
    candidate.name = "cdc_acm";
    f.pal.seedDriver("/sys/devices/u1", candidate);
    auto window = f.makeWindow();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    f.textAnswer.clear();  // user cancels — prefill observed, no callback
    window.bindAction()->trigger();
    EXPECT_EQ(f.lastTextPrefill, QStringLiteral("cdc_acm"));
    EXPECT_TRUE(f.bindDriverCalls.empty());
}

// ----- T12: destruction-order / teardown stress -----

// The T12 analogue of T11's 25× 'mq' + 15× 'mmq' stress runs, in-process:
// repeated full-window construction, Modules-tab entry (arming a REAL
// in-flight signature fill on the scheduler plus queued Qt posts), and
// destruction — varying how much of the queue was delivered beforehand. The
// window (and its ModuleListModel) dies first, then the fixture unwinds
// modulesVm BEFORE dispatcher/scheduler — the composition root's contract.
TEST(MainWindowTest, TeardownStressWithInFlightSignatureFill) {
    for (int i = 0; i < 25; ++i) {
        {
            Fixture f;
            for (int m = 0; m < 6; ++m) f.seedModule("mod" + std::to_string(m), m % 2);
            {
                auto window = f.makeWindow();
                window.tabs()->setCurrentIndex(1);  // banner + rebuild + fillSignatures
                if (i % 2 == 0) QCoreApplication::processEvents();
                if (i % 3 == 0) window.tabs()->setCurrentIndex(0);  // tab-flip variant
            }  // window destroyed with the fill possibly still in flight
            // Deliver whatever is still queued while the VM is alive but the
            // window's ModuleListModel is gone: the merge's rebuild must find
            // unregistered hooks, not the destroyed model (the ~ModuleListModel
            // unhook contract).
            QCoreApplication::processEvents();
        }  // fixture unwinds: modulesVm dtor waits the worker, dispatcher after
        QCoreApplication::processEvents();  // posts to dead dispatchers were dropped
    }
}

// Repeated tab flips over one live window: every entry re-arms banner/rebuild/
// fill (coalesced on the in-flight worker), interleaved with partial event
// delivery — then the queue is drained and the model must still mirror the VM.
TEST(MainWindowTest, RepeatedTabFlipsStayCoalescedAndConsistent) {
    Fixture f;
    for (int m = 0; m < 6; ++m) f.seedModule("mod" + std::to_string(m), m % 2);
    auto window = f.makeWindow();

    for (int i = 0; i < 15; ++i) {
        window.tabs()->setCurrentIndex(1);
        if (i % 2 == 0) QCoreApplication::processEvents();
        window.tabs()->setCurrentIndex(0);
    }
    f.modulesVm.fillSignatures().wait();
    QCoreApplication::processEvents();
    EXPECT_EQ(window.modulesView()->model()->rowCount(),
              static_cast<int>(f.modulesVm.rowsRef().size()));
}
