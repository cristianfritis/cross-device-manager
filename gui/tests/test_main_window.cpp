#include <atomic>
#include <condition_variable>
#include <mutex>
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
#include <QTextEdit>
#include <QToolBar>
#include <QTreeWidget>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/device_service.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/snapshots_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/app/updates_vm.hpp"
#include "devmgr/core/events.hpp"
#include "devmgr/core/snapshot_models.hpp"
#include "devmgr/core/update_models.hpp"
#include "devmgr/pal/criticality.hpp"
#include "devmgr/pal/hotplug_event.hpp"
#include "devmgr/runtime/delayed_scheduler.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "devmgr/runtime/task_scheduler.hpp"
#include "fakes/fake_criticality_prober.hpp"
#include "fakes/fake_pal.hpp"
#include "fakes/fake_privileged_channel.hpp"
#include "fakes/fake_update_provider.hpp"
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

// One updatable candidate; `localCab` selects the review-test-4 branch: a
// remote-only release is never installable (V1), a local one always is.
core::UpdateCandidate updateCandidate(std::string name, std::string current, std::string next,
                                      bool localCab) {
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
    r.localCab = localCab;
    if (!localCab) r.locations = {"https://example.org/fw.cab"};
    c.releases.push_back(r);
    return c;
}

// One snapshot meta; `idFill`/`health` vary the identity and the restore/delete
// verb gating (Ok restores + deletes, Corrupt refuses restore, Unsupported
// refuses both) — the same shapes test_snapshots_vm.cpp pins at the VM level.
core::SnapshotMeta snapMeta(char idFill, core::SnapshotHealth health = core::SnapshotHealth::Ok) {
    core::SnapshotMeta m;
    m.id = std::string(64, idFill);
    m.createdAtUtc = 1600000000;  // 2020-09-13 12:26:40 UTC
    m.trigger = core::SnapshotTrigger::Manual;
    m.reason = {.verb = "", .subject = "pre-upgrade"};
    m.health = health;
    m.entryCount = 1;
    m.modprobeFileCount = 0;
    return m;
}

struct Fixture {
    runtime::EventBus bus;
    runtime::TaskScheduler scheduler{2};
    runtime::DelayedScheduler delayed;
    test::FakePal pal;
    test::FakeCriticalityProber prober;
    app::DeviceService svc{bus};
    tests::FakeUpdateProvider provider;
    // Real channel so the Snapshots verbs reach a scriptable seam; its default
    // empty disabledEntries makes applyDisabledOverlay a no-op, so the device
    // tests are unaffected (the overlay only ever marks devices Disabled).
    test::FakePrivilegedChannel channel;
    gui::QtUiDispatcher dispatcher;
    app::ApplicationFacade facade{pal,     scheduler, bus,  svc,        &channel,
                                  &prober, &pal,      &pal, {&provider}};
    app::DeviceListVM listVm{facade, bus, dispatcher};
    app::DeviceDetailVM detailVm{facade};
    app::StatusLineVM statusVm{bus, delayed, dispatcher};
    // Declared after the dispatcher so it is destroyed first: the ModulesVM/
    // UpdatesVM/SnapshotsVM dtors' alive_-token store and future waits must run
    // while dispatcher and scheduler are still alive — the composition root's
    // declaration-order contract, reproduced at fixture scope.
    app::ModulesVM modulesVm{facade, bus, scheduler, dispatcher};
    app::UpdatesVM updatesVm{facade, bus, dispatcher};
    app::SnapshotsVM snapshotsVm{facade, bus, dispatcher};
    int refreshCalls = 0;
    int confirmCalls = 0;
    int confirmQuitCalls = 0;
    std::vector<std::pair<std::string, bool>> setEnabledCalls;
    std::vector<std::string> loadModuleCalls;
    std::vector<std::string> unloadModuleCalls;
    std::vector<std::pair<std::string, std::string>> bindDriverCalls;
    std::vector<std::string> unbindDriverCalls;
    bool confirmAnswer = true;
    bool confirmQuitAnswer = true;
    QString textAnswer;         // returned by the injected textInput seam
    QString lastTextPrefill;    // captured prefill the dialog would have shown
    QString lastConfirmPrompt;  // captured prompt — the restore preview body

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
        actions.confirm = [this](const QString& prompt) {
            ++confirmCalls;
            lastConfirmPrompt = prompt;
            return confirmAnswer;
        };
        actions.textInput = [this](const QString&, const QString& prefill) {
            lastTextPrefill = prefill;
            return textAnswer;
        };
        actions.confirmQuit = [this](const QString&) {
            ++confirmQuitCalls;
            return confirmQuitAnswer;
        };
        return gui::MainWindow(facade, listVm, detailVm, statusVm, modulesVm, updatesVm,
                               snapshotsVm, dispatcher, bus, std::move(actions));
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
    // Seeds one update candidate and runs refreshUpdates() to completion so
    // the facade's snapshot reflects it before a window is built/tab-entered.
    void seedUpdateAndRefresh(bool localCab) {
        provider.enumerateResult_ = std::vector<core::UpdateCandidate>{
            updateCandidate("Webcam", "1.2.2", "1.2.4", localCab)};
        facade.refreshUpdates().wait();
    }
    // Scripts the channel's snapshot list and runs refreshSnapshots() to
    // completion so the facade copy (and thus the model's ctor rebuild) reflects
    // it before a window is built/tab-entered — the snapshot analogue of
    // seedUpdateAndRefresh above.
    void seedSnapshotsAndRefresh(std::vector<core::SnapshotMeta> metas) {
        channel.snapshotMetas = std::move(metas);
        facade.refreshSnapshots().wait();
    }
    // Triggers a snapshot mutation action and blocks until its (worker-thread)
    // TaskCompletedEvent fires, so the channel's snapshotCalls write
    // happens-before the assertion reads it (the mutation records into the
    // channel before publishing completion — no data race).
    // The restore preview and the diff pane both wait on an async
    // SnapshotDiffRefreshedEvent that arrives via the dispatcher, so tests pump
    // the Qt loop until the VM reports the fetch finished. Bounded so a genuine
    // failure fails the test instead of hanging it.
    void pumpUntilDiffLanded() {
        for (int i = 0; i < 1000 && channel.snapshotCalls.empty(); ++i)
            QCoreApplication::processEvents();
        for (int i = 0; i < 1000; ++i) QCoreApplication::processEvents();
    }
    // Same pump, named for the restore path: it ends with the confirm seam
    // having been called (or not, if the diff never landed).
    void pumpUntilPreviewDialog() { pumpUntilDiffLanded(); }

    void triggerAndAwaitMutation(QAction* action) {
        std::atomic<bool> done{false};
        auto sub = bus.subscribe<core::TaskCompletedEvent>(
            [&](const core::TaskCompletedEvent&) { done.store(true); });
        action->trigger();
        while (!done.load()) QCoreApplication::processEvents();
        QCoreApplication::processEvents();  // drain the completion/refresh posts
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

    ASSERT_EQ(window.tabs()->count(), 4);  // Devices | Modules | Updates | Snapshots
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
    // The refusal now flows through StatusLineVM (Phase 5 review F-1), which
    // ignores events until armed — same as the composition root arms it right
    // after the initial refresh.
    f.statusVm.arm();
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
    // The refusal now flows through StatusLineVM (Phase 5 review F-1), which
    // ignores events until armed — same as the composition root arms it right
    // after the initial refresh.
    f.statusVm.arm();

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

// ----- T12: Updates tab -----

TEST(MainWindowTest, InstallActionDisabledForRemoteOnlyRelease) {  // review test 4, GUI half
    Fixture f;
    f.seedUpdateAndRefresh(/*localCab=*/false);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(2);  // rebuild + refresh on entry
    ASSERT_EQ(window.updatesView()->model()->rowCount(), 1);
    EXPECT_FALSE(window.installUpdateAction()->isEnabled());
}

TEST(MainWindowTest, InstallActionEnabledOnUpdatesTabWithLocalCab) {
    Fixture f;
    f.seedUpdateAndRefresh(/*localCab=*/true);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(2);
    ASSERT_EQ(window.updatesView()->model()->rowCount(), 1);
    EXPECT_TRUE(window.installUpdateAction()->isEnabled());
}

TEST(MainWindowTest, QuitGuardBlocksCloseDuringInstall) {
    Fixture f;
    f.seedUpdateAndRefresh(/*localCab=*/true);
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(2);

    // Latch-blocked install (fake install() body blocks until released) — the
    // way to drive facade_.installActive() true for the duration of the test,
    // per the FakeUpdateProvider reuse note.
    std::mutex m;
    std::condition_variable cv;
    bool release = false;
    f.provider.onInstall_ = [&](auto&) {
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return release; });
    };

    window.installUpdateAction()->trigger();  // confirm=true by default
    ASSERT_TRUE(f.facade.installActive());

    f.confirmQuitAnswer = false;
    window.close();
    EXPECT_TRUE(window.isVisible());
    EXPECT_EQ(f.confirmQuitCalls, 1);

    f.confirmQuitAnswer = true;
    window.close();
    EXPECT_FALSE(window.isVisible());
    EXPECT_EQ(f.confirmQuitCalls, 2);

    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    cv.notify_all();
    QCoreApplication::processEvents();  // deliver the install's completion posts
}

// Review finding I-1 (parity gap, DESIGN.md §9 Task feedback row): the GUI
// status bar must fold in UpdatesVM::installProgressText() while the Updates
// tab is current, exactly like tui_app.cpp's updatesStatusLine() folds it
// into the FTXUI bottom status line — otherwise a multi-minute firmware flash
// shows nothing until it completes.
TEST(MainWindowTest, StatusBarShowsInstallProgressThenRevertsOnCompletion) {
    Fixture f;
    f.seedUpdateAndRefresh(/*localCab=*/true);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(2);

    // Latch-blocked install (the FakeUpdateProvider reuse note pinned by
    // QuitGuardBlocksCloseDuringInstall above), extended to emit one progress
    // update before blocking so the durable text is observably non-empty for
    // the whole (real, cross-thread) install duration.
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> progressed{false};
    bool release = false;
    f.provider.onInstall_ = [&](runtime::ProgressReporter& progress) {
        progress(runtime::ProgressUpdate{.percent = 10, .stage = "device-write"});
        progressed.store(true);
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&] { return release; });
    };

    window.installUpdateAction()->trigger();  // confirm=true by default
    ASSERT_TRUE(f.facade.installActive());

    // The progress post crosses threads (application_facade.cpp publishes on
    // the scheduler worker) — spin until it lands, then drain the dispatcher
    // (QtUiDispatcher queues cross-thread posts; same-thread posts run
    // directly, as the comment on StatusBarShowsTransientHotplugMessage notes).
    while (!progressed.load()) QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    const std::string progressText = f.updatesVm.installProgressText();
    ASSERT_FALSE(progressText.empty());
    EXPECT_EQ(window.statusBar()->currentMessage().toStdString(), progressText);

    {
        std::lock_guard<std::mutex> lock(m);
        release = true;
    }
    cv.notify_all();
    // installActive() flips false only after the worker's TaskCompletedEvent
    // (and UpdatesVM's progressText_.clear()) have already run (InstallActiveGuard
    // releases the slot last) — so this spin is a safe completion barrier.
    while (f.facade.installActive()) QCoreApplication::processEvents();
    QCoreApplication::processEvents();  // drain the completion post → taskExecuted

    EXPECT_TRUE(f.updatesVm.installProgressText().empty());
    EXPECT_EQ(window.statusBar()->currentMessage().toStdString(), f.statusVm.text());
}

TEST(MainWindowTest, RequestBannerVisibleUntilDismissed) {
    Fixture f;
    auto window = f.makeWindow();
    window.show();  // isVisible() reflects the whole ancestor chain — needed below
    window.tabs()->setCurrentIndex(2);
    EXPECT_FALSE(window.requestBannerLabel()->isVisible());

    // Same-thread publish → UpdatesVM::onRequest → postWake() runs directly
    // (dispatcher is on this thread) → taskExecuted → the window's Updates-tab
    // wake handler refreshes the request banner (T11 lesson: not tab-entry-only).
    f.bus.publish(core::UpdateRequestEvent{.providerId = "fake",
                                           .deviceId = "a1",
                                           .kind = "post",
                                           .message = "unplug and replug the device"});
    EXPECT_TRUE(window.requestBannerLabel()->isVisible());
    EXPECT_TRUE(window.requestBannerLabel()->text().contains(QStringLiteral("unplug and replug")));

    // Progress events must not hide it — durable until dismiss (spec §9).
    f.bus.publish(core::TaskProgressEvent{
        .taskId = "install-update:a1", .percent = 10, .stage = "device-write"});
    EXPECT_TRUE(window.requestBannerLabel()->isVisible());

    window.dismissRequestAction()->trigger();
    EXPECT_FALSE(window.requestBannerLabel()->isVisible());
}

TEST(MainWindowTest, GuardRefusalGoesThroughStatusLineVM) {  // pins T1 F-1 for Updates
    Fixture f;
    f.seedUpdateAndRefresh(/*localCab=*/false);  // remote-only → selectedInstall() == nullopt
    auto window = f.makeWindow();
    f.statusVm.arm();
    window.tabs()->setCurrentIndex(2);

    // QAction::trigger() no-ops while disabled, and updateActionEnablement()
    // already disables this action for the identical reason (selectedInstall()
    // == nullopt). Force it enabled to reach the handler's own re-check: the
    // guard-refusal branch is defense in depth, not merely a restatement of
    // the enablement condition, and must publish through the bus either way.
    window.installUpdateAction()->setEnabled(true);
    window.installUpdateAction()->trigger();
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("not installable")));

    // An unrelated wake must not wipe the refusal — StatusLineVM owns the
    // status line (TTL + no wipe-by-wake), the same contract already pinned
    // for the module/unbind guard refusals above.
    f.dispatcher.post([] {});
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("not installable")));
}

// ----- Phase 7: Snapshots tab -----

TEST(MainWindowTest, SnapshotsTabAddedAndVerbsGatedToTab) {
    Fixture f;
    auto window = f.makeWindow();
    ASSERT_EQ(window.tabs()->count(), 4);
    EXPECT_EQ(window.tabs()->tabText(3), QStringLiteral("Snapshots"));

    // Off the Snapshots tab (Devices): every verb disabled.
    EXPECT_FALSE(window.createSnapshotAction()->isEnabled());
    EXPECT_FALSE(window.restoreSnapshotAction()->isEnabled());
    EXPECT_FALSE(window.deleteSnapshotAction()->isEnabled());
    EXPECT_FALSE(window.diffSnapshotAction()->isEnabled());
    EXPECT_FALSE(window.historySnapshotAction()->isEnabled());

    window.tabs()->setCurrentIndex(3);
    // On the tab, the verbs are live; the per-selection refusal is enforced on
    // click (TUI parity), not by greying the action out.
    EXPECT_TRUE(window.createSnapshotAction()->isEnabled());
    EXPECT_TRUE(window.restoreSnapshotAction()->isEnabled());
    EXPECT_TRUE(window.deleteSnapshotAction()->isEnabled());
    EXPECT_TRUE(window.diffSnapshotAction()->isEnabled());
    EXPECT_TRUE(window.historySnapshotAction()->isEnabled());
}

TEST(MainWindowTest, SnapshotsTabEntrySetsBannerAndRows) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a'), snapMeta('b', core::SnapshotHealth::Corrupt)});
    auto window = f.makeWindow();

    window.tabs()->setCurrentIndex(3);  // banner + rebuild + refreshSnapshots
    // Byte-frozen banner rendered exactly as the VM emits it.
    EXPECT_EQ(window.snapshotsBannerLabel()->text(),
              QString::fromStdString(f.snapshotsVm.banner()));
    auto* model = window.snapshotsView()->model();
    ASSERT_EQ(model->rowCount(), 2);
    // Byte-frozen row fidelity end to end (VM → Qt model → view data).
    EXPECT_EQ(model->data(model->index(0, 0), Qt::DisplayRole).toString().toStdString(),
              f.snapshotsVm.rowsRef()[0]);
}

TEST(MainWindowTest, SnapshotDetailPaneShowsSelectedSnapshot) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));

    auto* tree = window.snapshotsDetailTree();
    ASSERT_GE(tree->topLevelItemCount(), 1);
    // First detail line is "Id:      <id>" → split into ("Id", "<id>").
    EXPECT_EQ(tree->topLevelItem(0)->text(0), QStringLiteral("Id"));
    EXPECT_EQ(tree->topLevelItem(0)->text(1), QString::fromStdString(std::string(64, 'a')));
}

TEST(MainWindowTest, CreateSnapshotFlowsThroughTextInputAndFacade) {
    Fixture f;
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);

    f.textAnswer = QStringLiteral("pre-upgrade");
    f.triggerAndAwaitMutation(window.createSnapshotAction());
    ASSERT_EQ(f.channel.snapshotCalls.size(), 1u);
    EXPECT_EQ(f.channel.snapshotCalls[0], "create:pre-upgrade");
}

TEST(MainWindowTest, RestoreConfirmedInvokesFacadeWithSelectedId) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));

    // Restore now goes through the preview: trigger fetches the diff, and the
    // confirmation dialog opens only once it has landed (beta-06 task 3.3).
    f.triggerAndAwaitMutation(window.restoreSnapshotAction());
    EXPECT_EQ(f.confirmCalls, 1);
    ASSERT_EQ(f.channel.snapshotCalls.size(), 2u);
    EXPECT_EQ(f.channel.snapshotCalls[0], "diff:" + std::string(64, 'a') + ":live");
    EXPECT_EQ(f.channel.snapshotCalls[1], "restore:" + std::string(64, 'a'));
}

TEST(MainWindowTest, DeclinedRestoreSendsNothing) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));

    f.confirmAnswer = false;
    window.restoreSnapshotAction()->trigger();
    f.pumpUntilPreviewDialog();
    EXPECT_EQ(f.confirmCalls, 1);
    // The diff read happened; the restore did not.
    EXPECT_EQ(f.channel.snapshotCalls.size(), 1u);
    EXPECT_EQ(f.channel.snapshotCalls[0], "diff:" + std::string(64, 'a') + ":live");
}

// The preview carries what the spec requires before a restore can be confirmed:
// the pending change, which snapshot is selected/HEAD/last-good, and the
// partial-convergence note — all of it the VM's wording, rendered verbatim.
TEST(MainWindowTest, RestorePreviewDialogShowsDiffMarkersAndConvergenceNote) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    core::SnapshotDiff diff;
    diff.baseId = std::string(64, 'a');
    diff.entries.push_back({.kind = core::kDiffKindDevice,
                            .key = "usb 1d6b:0002 @2-1",
                            .before = "disabled (authorized)",
                            .after = core::kDiffStateAbsent});
    f.channel.nextDiff = diff;

    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));
    f.confirmAnswer = false;
    window.restoreSnapshotAction()->trigger();
    f.pumpUntilPreviewDialog();

    ASSERT_EQ(f.confirmCalls, 1);
    const QString prompt = f.lastConfirmPrompt;
    EXPECT_TRUE(prompt.contains(QStringLiteral("Restore snapshot aaaaaaaaaaaa?")));
    EXPECT_TRUE(prompt.contains(QStringLiteral("Current HEAD:")));
    EXPECT_TRUE(prompt.contains(QStringLiteral("Last good:")));
    EXPECT_TRUE(prompt.contains(QStringLiteral("usb 1d6b:0002 @2-1")));
    EXPECT_TRUE(prompt.contains(QStringLiteral("Convergence may be partial")));
}

// Parity with the TUI 'd' key: the diff pane names the snapshot it describes
// and renders the VM's lines verbatim.
TEST(MainWindowTest, DiffActionShowsDiffPaneForSelectedSnapshot) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    core::SnapshotDiff diff;
    diff.baseId = std::string(64, 'a');
    diff.entries.push_back({.kind = core::kDiffKindModule,
                            .key = "nouveau",
                            .before = core::kDiffStateBlacklisted,
                            .after = core::kDiffStateAbsent});
    f.channel.nextDiff = diff;

    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));
    window.diffSnapshotAction()->trigger();
    f.pumpUntilDiffLanded();

    const QString shown = window.snapshotDiffView()->toPlainText();
    EXPECT_TRUE(shown.contains(QStringLiteral("Differences: aaaaaaaaaaaa -> current state")));
    EXPECT_TRUE(shown.contains(QStringLiteral("nouveau")));
    // Toggling off returns to the detail pane.
    window.diffSnapshotAction()->trigger();
    EXPECT_GE(window.snapshotsDetailTree()->topLevelItemCount(), 1);
}

// Parity with the TUI 'h' key: history markers reach the Qt model verbatim.
TEST(MainWindowTest, HistoryActionTogglesChainMarkersInTheRows) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);

    // trigger() toggles a checkable action and delivers the new state, so the
    // single call is what turns history on.
    window.historySnapshotAction()->trigger();
    ASSERT_TRUE(window.historySnapshotAction()->isChecked());
    QCoreApplication::processEvents();
    auto* model = window.snapshotsView()->model();
    ASSERT_GE(model->rowCount(), 1);
    EXPECT_TRUE(model->data(model->index(0, 0), Qt::DisplayRole)
                    .toString()
                    .contains(QStringLiteral("[chain start, HEAD, last good]")));
}

// The filter field is the Devices/Modules interaction, applied to Snapshots.
TEST(MainWindowTest, SnapshotFilterNarrowsRowsAndNamesAnEmptyResult) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);

    window.snapshotFilterEdit()->setText(QStringLiteral("zzz"));
    QCoreApplication::processEvents();
    auto* model = window.snapshotsView()->model();
    ASSERT_EQ(model->rowCount(), 1);
    EXPECT_EQ(model->data(model->index(0, 0), Qt::DisplayRole).toString(),
              QStringLiteral("No snapshots match \"zzz\""));
}

// A restore that leaves items unconverged must surface the way back — failed
// item, safety id and the exact CLI command — not a bare error.
TEST(MainWindowTest, UnconvergedRestoreShowsRecoveryGuidance) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    core::RestoreOutcome outcome;
    outcome.snapshotId = std::string(64, 'a');
    outcome.safetySnapshotId = std::string(64, 'e');
    outcome.items.push_back({.subject = "/sys/devices/usb1/1-2",
                             .action = "re-apply-disable",
                             .status = "guard-refused",
                             .detail = "only remaining keyboard"});
    f.channel.nextRestore = outcome;

    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));
    EXPECT_FALSE(window.snapshotGuidanceLabel()->isVisible());

    f.triggerAndAwaitMutation(window.restoreSnapshotAction());
    // The guidance surfaces on the post-restore rebuild
    // (SnapshotsChangedEvent → refresh → refreshed → modelReset), several
    // dispatcher hops after the mutation's completion event.
    for (int i = 0; i < 1000 && !window.snapshotGuidanceLabel()->isVisible(); ++i)
        QCoreApplication::processEvents();

    const QString guidance = window.snapshotGuidanceLabel()->text();
    EXPECT_TRUE(guidance.contains(QStringLiteral("guard-refused")));
    EXPECT_TRUE(guidance.contains(QStringLiteral("only remaining keyboard")));
    EXPECT_TRUE(guidance.contains(QStringLiteral("eeeeeeeeeeee")));
    EXPECT_TRUE(guidance.contains(QStringLiteral("devmgr snapshot restore eeeeeeeeeeee")));
}

TEST(MainWindowTest, DeleteConfirmedInvokesFacadeWithSelectedId) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a')});
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));

    f.triggerAndAwaitMutation(window.deleteSnapshotAction());
    EXPECT_EQ(f.confirmCalls, 1);
    ASSERT_EQ(f.channel.snapshotCalls.size(), 1u);
    EXPECT_EQ(f.channel.snapshotCalls[0], "delete:" + std::string(64, 'a'));
}

// The refusal path (task 5.2): a corrupt snapshot refuses restore locally. The
// verb stays enabled (TUI parity), so the click reaches the guard branch, which
// publishes through StatusLineVM — no confirm, no facade call. Delete on the
// same corrupt snapshot is still permitted.
TEST(MainWindowTest, CorruptSnapshotRestoreRefusedThroughStatusLineVM) {
    Fixture f;
    f.seedSnapshotsAndRefresh({snapMeta('a', core::SnapshotHealth::Corrupt)});
    auto window = f.makeWindow();
    f.statusVm.arm();
    window.tabs()->setCurrentIndex(3);
    window.snapshotsView()->setCurrentIndex(window.snapshotsView()->model()->index(0, 0));

    window.restoreSnapshotAction()->trigger();
    EXPECT_EQ(f.confirmCalls, 0);  // refusal short-circuits before the confirm
    EXPECT_TRUE(f.channel.snapshotCalls.empty());
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("cannot restore:")));
}

// The placeholder (empty) list refuses both verbs locally: the guard branch
// publishes the refusal instead of confirming or calling the facade.
TEST(MainWindowTest, PlaceholderSnapshotDeleteRefusedThroughStatusLineVM) {
    Fixture f;  // no snapshots seeded → "(no snapshots)" placeholder row
    auto window = f.makeWindow();
    f.statusVm.arm();
    window.tabs()->setCurrentIndex(3);

    window.deleteSnapshotAction()->trigger();
    EXPECT_EQ(f.confirmCalls, 0);
    EXPECT_TRUE(f.channel.snapshotCalls.empty());
    EXPECT_TRUE(window.statusBar()->currentMessage().contains(QStringLiteral("cannot delete:")));
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
