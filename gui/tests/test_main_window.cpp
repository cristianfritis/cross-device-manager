#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QAction>
#include <QCoreApplication>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QShortcut>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QRegularExpression>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QTest>

#include "devmgr/core/backend_wording.hpp"
#include "tests/fixtures/backend_sentences.hpp"

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
// The raw shape fwupd::mapError composes for an absent service — what the beta
// user saw verbatim in the Updates banner.
constexpr const char* kRawServiceUnknown =
    "org.freedesktop.DBus.Error.ServiceUnknown: The name org.freedesktop.fwupd was not provided "
    "by any .service files";
// What the privileged channel hands back when the bus cannot reach devmgrd
// (platform/linux/.../dbus_contract.hpp) — diagnostic, never presentation.
constexpr const char* kRawDaemonUnavailable = "helper devmgrd is not available";

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
    // Nothing selected is a state of the selection, not of the tab: the verb
    // belongs to Devices, so it stays present and explains itself by being off.
    EXPECT_TRUE(window.toggleAction()->isVisible());

    f.refreshAndPump();
    const int row = f.firstDeviceRow();
    ASSERT_GE(row, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(row, 0));
    EXPECT_TRUE(window.toggleAction()->isEnabled());
    EXPECT_TRUE(window.toggleAction()->isVisible());
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
    // Devices tab: module actions are not merely off, they are gone — a verb from
    // another tab is absent (DESIGN.md §5.3). Device actions follow the selection
    // and stay present while they wait for one.
    EXPECT_FALSE(window.loadModuleAction()->isEnabled());
    EXPECT_FALSE(window.loadModuleAction()->isVisible());
    EXPECT_FALSE(window.unloadModuleAction()->isEnabled());
    EXPECT_FALSE(window.unloadModuleAction()->isVisible());
    EXPECT_FALSE(window.unbindAction()->isEnabled());  // nothing selected yet
    EXPECT_TRUE(window.unbindAction()->isVisible());
    EXPECT_FALSE(window.bindAction()->isEnabled());
    EXPECT_TRUE(window.bindAction()->isVisible());
    f.selectFirstDevice(window);
    EXPECT_TRUE(window.unbindAction()->isEnabled());
    EXPECT_TRUE(window.bindAction()->isEnabled());

    window.tabs()->setCurrentIndex(1);
    EXPECT_TRUE(window.loadModuleAction()->isEnabled());
    EXPECT_TRUE(window.loadModuleAction()->isVisible());
    // The VM starts on row 0 = "dummy" → unload has a target.
    EXPECT_TRUE(window.unloadModuleAction()->isEnabled());
    EXPECT_TRUE(window.unloadModuleAction()->isVisible());
    EXPECT_FALSE(window.bindAction()->isEnabled());  // device actions off on Modules tab
    EXPECT_FALSE(window.bindAction()->isVisible());
    EXPECT_FALSE(window.unbindAction()->isEnabled());
    EXPECT_FALSE(window.unbindAction()->isVisible());
    EXPECT_FALSE(window.toggleAction()->isEnabled());
    EXPECT_FALSE(window.toggleAction()->isVisible());

    window.tabs()->setCurrentIndex(0);
    EXPECT_FALSE(window.loadModuleAction()->isEnabled());
    EXPECT_FALSE(window.loadModuleAction()->isVisible());
    EXPECT_TRUE(window.bindAction()->isEnabled());  // device selection retained
    EXPECT_TRUE(window.bindAction()->isVisible());
}

TEST(MainWindowTest, PlaceholderRowDisablesUnload) {
    Fixture f;  // no modules seeded → "(no modules)" placeholder row
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(1);
    EXPECT_TRUE(window.loadModuleAction()->isEnabled());
    // A placeholder row is no target, but Unload still belongs to this tab, so it
    // stays present and off rather than vanishing.
    EXPECT_FALSE(window.unloadModuleAction()->isEnabled());
    EXPECT_TRUE(window.unloadModuleAction()->isVisible());
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
    d.nativeId = "/sys/devices/root-disk";
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
    d.nativeId = "/sys/devices/u1";
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
    // A remote-only release is a refusal about the selected candidate, not an
    // inapplicable verb: it stays on the toolbar to be explained.
    EXPECT_TRUE(window.installUpdateAction()->isVisible());
}

TEST(MainWindowTest, InstallActionEnabledOnUpdatesTabWithLocalCab) {
    Fixture f;
    f.seedUpdateAndRefresh(/*localCab=*/true);
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(2);
    ASSERT_EQ(window.updatesView()->model()->rowCount(), 1);
    EXPECT_TRUE(window.installUpdateAction()->isEnabled());
    EXPECT_TRUE(window.installUpdateAction()->isVisible());
}

// ----- Backend unavailability: sentence visible, raw detail demoted -----

// The shipped defect, from the GUI side: an unreachable fwupd put
// "org.freedesktop.DBus.Error.ServiceUnknown: …" straight into the banner, and
// the list still claimed "(no updates available)" as though the query had run.
TEST(MainWindowTest, UnreachableProviderShowsSentenceNotRawDbusName) {
    Fixture f;
    f.provider.id_ = "fwupd";
    f.provider.availability_ = {
        .available = false,
        .version = {},
        .error = core::Error{.code = core::Error::Code::Io, .message = kRawServiceUnknown},
        .notices = {}};
    f.facade.refreshUpdates().wait();
    auto window = f.makeWindow();
    window.show();  // child visibility is only meaningful once the window is up
    window.tabs()->setCurrentIndex(2);

    const QString banner = window.updatesBannerLabel()->text();
    static const QRegularExpression raw(
        QStringLiteral("org\\.freedesktop|DBus\\.Error|ServiceUnknown|errno"));
    EXPECT_FALSE(banner.contains(raw)) << banner.toStdString();
    // The shared fixture constant, byte-pinned to the core table by
    // tests/unit/test_backend_parity.cpp — the same string the TUI render test
    // asserts, which is what makes the two surfaces provably agree.
    EXPECT_TRUE(banner.contains(QString::fromUtf8(tests::kFwupdUnreachableSentence)));
    EXPECT_TRUE(banner.contains(QStringLiteral("?")));        // glyph, not colour (§9 exception)
    EXPECT_TRUE(window.updatesBannerLabel()->font().bold());  // weight carries the warning role

    // No primary widget claims a completed, empty query.
    for (int row = 0; row < window.updatesView()->model()->rowCount(); ++row)
        EXPECT_FALSE(window.updatesView()->model()->index(row, 0).data().toString().contains(
            QStringLiteral("(no updates available)")));

    // The detail is demoted, not deleted — and stays hidden until asked for.
    ASSERT_TRUE(window.updatesDetailsButton()->isVisible());
    // Staged but not shown: the raw text exists in the collapsed region only,
    // which renders nothing until the disclosure is opened.
    EXPECT_FALSE(window.updatesDiagnosticLabel()->isVisible());
}

TEST(MainWindowTest, DisclosureRevealsTheRawDiagnostic) {
    Fixture f;
    f.provider.id_ = "fwupd";
    f.provider.availability_ = {
        .available = false,
        .version = {},
        .error = core::Error{.code = core::Error::Code::Io, .message = kRawServiceUnknown},
        .notices = {}};
    f.facade.refreshUpdates().wait();
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(2);

    window.updatesDetailsButton()->setChecked(true);
    EXPECT_TRUE(window.updatesDiagnosticLabel()->isVisible());
    EXPECT_TRUE(window.updatesDiagnosticLabel()->text().contains(
        QString::fromStdString(kRawServiceUnknown)));
    // Read-only, and reachable without a pointer.
    EXPECT_TRUE(window.updatesDiagnosticLabel()->textInteractionFlags() &
                Qt::TextSelectableByKeyboard);

    window.updatesDetailsButton()->setChecked(false);
    EXPECT_FALSE(window.updatesDiagnosticLabel()->isVisible());
}

// Keyboard-only path: the disclosure is in the tab order and toggles from a key
// event alone, so the diagnostic is never pointer-gated (ui-accessibility).
TEST(MainWindowTest, DisclosureIsReachableAndToggledByKeyboardAlone) {
    Fixture f;
    f.provider.id_ = "fwupd";
    f.provider.availability_ = {
        .available = false,
        .version = {},
        .error = core::Error{.code = core::Error::Code::Io, .message = kRawServiceUnknown},
        .notices = {}};
    f.facade.refreshUpdates().wait();
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(2);

    EXPECT_FALSE(window.updatesDetailsButton()->accessibleName().isEmpty());
    ASSERT_NE(window.updatesDetailsButton()->focusPolicy() & Qt::TabFocus, Qt::NoFocus);

    window.updatesDetailsButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(window.updatesDetailsButton(), Qt::Key_Space);
    EXPECT_TRUE(window.updatesDiagnosticLabel()->isVisible());
    QTest::keyClick(window.updatesDetailsButton(), Qt::Key_Space);
    EXPECT_FALSE(window.updatesDiagnosticLabel()->isVisible());
}

// Healthy providers: no note, so no disclosure control and no region — the
// affordance collapses to nothing rather than sitting there disabled.
TEST(MainWindowTest, HealthyProvidersShowNoDisclosure) {
    Fixture f;
    f.provider.id_ = "fwupd";
    f.seedUpdateAndRefresh(/*localCab=*/true);
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(2);

    EXPECT_FALSE(window.updatesDetailsButton()->isVisible());
    EXPECT_FALSE(window.updatesDiagnosticLabel()->isVisible());
    EXPECT_FALSE(window.updatesBannerLabel()->font().bold());
}

// ----- Backend unavailability: the daemon, on the view it feeds (§13) --------

// The Snapshots list comes from devmgrd, so an unreachable daemon is explained
// here — with the same sentence, glyph, weight and disclosure the Updates page
// uses for fwupd. One accessor, two pages, no page-specific wording.
TEST(MainWindowTest, UnreachableDaemonShowsSentenceOnSnapshotsPage) {
    Fixture f;
    f.channel.snapshotMetas = core::makeError(core::Error::Code::Io, kRawDaemonUnavailable);
    f.facade.refreshSnapshots().wait();
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(3);

    const QString banner = window.snapshotsBannerLabel()->text();
    static const QRegularExpression raw(
        QStringLiteral("org\\.freedesktop|DBus\\.Error|ServiceUnknown|errno"));
    EXPECT_FALSE(banner.contains(raw)) << banner.toStdString();
    EXPECT_TRUE(banner.contains(QString::fromUtf8(tests::kDevmgrdUnreachableSentence)));
    EXPECT_TRUE(banner.contains(QStringLiteral("?")));
    EXPECT_TRUE(window.snapshotsBannerLabel()->font().bold());
    // The raw diagnostic is not on the primary surface either.
    EXPECT_FALSE(banner.contains(QString::fromStdString(kRawDaemonUnavailable)));

    // No row claims a completed, empty query.
    for (int row = 0; row < window.snapshotsView()->model()->rowCount(); ++row)
        EXPECT_FALSE(window.snapshotsView()->model()->index(row, 0).data().toString().contains(
            QStringLiteral("(no snapshots)")));

    ASSERT_TRUE(window.snapshotsDetailsButton()->isVisible());
    EXPECT_FALSE(window.snapshotsDiagnosticLabel()->isVisible());
}

TEST(MainWindowTest, SnapshotsDisclosureRevealsTheRawDaemonDiagnostic) {
    Fixture f;
    f.channel.snapshotMetas = core::makeError(core::Error::Code::Io, kRawDaemonUnavailable);
    f.facade.refreshSnapshots().wait();
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(3);

    // Keyboard alone, like the Updates disclosure: named, focusable, togglable.
    EXPECT_FALSE(window.snapshotsDetailsButton()->accessibleName().isEmpty());
    ASSERT_NE(window.snapshotsDetailsButton()->focusPolicy() & Qt::TabFocus, Qt::NoFocus);
    window.snapshotsDetailsButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(window.snapshotsDetailsButton(), Qt::Key_Space);

    EXPECT_TRUE(window.snapshotsDiagnosticLabel()->isVisible());
    EXPECT_TRUE(window.snapshotsDiagnosticLabel()->text().contains(
        QString::fromStdString(kRawDaemonUnavailable)));

    QTest::keyClick(window.snapshotsDetailsButton(), Qt::Key_Space);
    EXPECT_FALSE(window.snapshotsDiagnosticLabel()->isVisible());
}

// §14 F1/F2. The live matrix found the GUI carrying the note on Snapshots and
// Updates but NOT on Devices (no banner existed at all) and only half-carrying
// it on Modules (the plain string, so no glyph, no weight, no disclosure) —
// while the TUI carried it fully on all three. The daemon owns every mutation
// verb on Devices and Modules, so a user looking at dimmed controls is owed the
// reason on the tab they are standing on.
TEST(MainWindowTest, UnreachableDaemonShowsSentenceOnDevicesPage) {
    Fixture f;
    f.channel.disabledEntries = core::makeError(core::Error::Code::Io, kRawDaemonUnavailable);
    f.facade.refresh().wait();
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(0);

    const QString banner = window.devicesBannerLabel()->text();
    static const QRegularExpression raw(
        QStringLiteral("org\\.freedesktop|DBus\\.Error|ServiceUnknown|errno"));
    EXPECT_FALSE(banner.contains(raw)) << banner.toStdString();
    EXPECT_TRUE(banner.contains(QString::fromUtf8(tests::kDevmgrdUnreachableSentence)));
    EXPECT_TRUE(banner.contains(QStringLiteral("?")));
    EXPECT_TRUE(window.devicesBannerLabel()->font().bold());
    EXPECT_FALSE(banner.contains(QString::fromStdString(kRawDaemonUnavailable)));

    // Reachable by keyboard alone, and the raw text is demoted, not deleted.
    ASSERT_TRUE(window.devicesDetailsButton()->isVisible());
    EXPECT_FALSE(window.devicesDiagnosticLabel()->isVisible());
    EXPECT_FALSE(window.devicesDetailsButton()->accessibleName().isEmpty());
    ASSERT_NE(window.devicesDetailsButton()->focusPolicy() & Qt::TabFocus, Qt::NoFocus);
    window.devicesDetailsButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(window.devicesDetailsButton(), Qt::Key_Space);
    EXPECT_TRUE(window.devicesDiagnosticLabel()->isVisible());
    EXPECT_TRUE(window.devicesDiagnosticLabel()->text().contains(
        QString::fromStdString(kRawDaemonUnavailable)));
}

TEST(MainWindowTest, UnreachableDaemonShowsSentenceOnModulesPage) {
    Fixture f;
    f.channel.disabledEntries = core::makeError(core::Error::Code::Io, kRawDaemonUnavailable);
    f.facade.refresh().wait();
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(1);

    const QString banner = window.bannerLabel()->text();
    static const QRegularExpression raw(
        QStringLiteral("org\\.freedesktop|DBus\\.Error|ServiceUnknown|errno"));
    EXPECT_FALSE(banner.contains(raw)) << banner.toStdString();
    EXPECT_TRUE(banner.contains(QString::fromUtf8(tests::kDevmgrdUnreachableSentence)));
    EXPECT_TRUE(banner.contains(QStringLiteral("?")));
    // The role now arrives with the text through bannerLine(), so the weight is
    // the VM's decision rather than a string the GUI re-read.
    EXPECT_TRUE(window.bannerLabel()->font().bold());

    ASSERT_TRUE(window.modulesDetailsButton()->isVisible());
    EXPECT_FALSE(window.modulesDiagnosticLabel()->isVisible());
    window.modulesDetailsButton()->setFocus(Qt::TabFocusReason);
    QTest::keyClick(window.modulesDetailsButton(), Qt::Key_Space);
    EXPECT_TRUE(window.modulesDiagnosticLabel()->isVisible());
    EXPECT_TRUE(window.modulesDiagnosticLabel()->text().contains(
        QString::fromStdString(kRawDaemonUnavailable)));
}

// The affordance collapses to nothing while the daemon serves — no empty row
// reserved, no inert button in the tab order.
TEST(MainWindowTest, HealthyDaemonShowsNoDevicesOrModulesDisclosure) {
    Fixture f;
    f.facade.refresh().wait();
    auto window = f.makeWindow();
    window.show();

    window.tabs()->setCurrentIndex(0);
    EXPECT_FALSE(window.devicesBannerLabel()->isVisible());
    EXPECT_FALSE(window.devicesDetailsButton()->isVisible());
    EXPECT_FALSE(window.devicesDiagnosticLabel()->isVisible());

    window.tabs()->setCurrentIndex(1);
    EXPECT_FALSE(window.modulesDetailsButton()->isVisible());
    EXPECT_FALSE(window.modulesDiagnosticLabel()->isVisible());
    EXPECT_FALSE(window.bannerLabel()->text().contains(QStringLiteral("?")));
}

TEST(MainWindowTest, HealthyDaemonShowsNoSnapshotsDisclosure) {
    Fixture f;
    f.facade.refreshSnapshots().wait();  // answered, and the store is empty
    auto window = f.makeWindow();
    window.show();
    window.tabs()->setCurrentIndex(3);

    EXPECT_FALSE(window.snapshotsDetailsButton()->isVisible());
    EXPECT_FALSE(window.snapshotsDiagnosticLabel()->isVisible());
    EXPECT_FALSE(window.snapshotsBannerLabel()->font().bold());
    // A completed query that found nothing still says so.
    ASSERT_EQ(window.snapshotsView()->model()->rowCount(), 1);
    EXPECT_EQ(window.snapshotsView()->model()->index(0, 0).data().toString(),
              QStringLiteral("(no snapshots)"));
}

// ----- Blocked verbs reuse the shared sentence (§5.3, §11) -------------------

// The catalog's #7/#8: a verb the daemon cannot serve stays VISIBLE and greyed,
// and says WHY in the shared words. Hidden-while-down and a blank reason are
// both defects — the first removes the affordance, the second removes the
// explanation, and either one sends the user to the logs.
TEST(MainWindowTest, DaemonDownDisablesVerbsVisiblyWithTheSharedReason) {
    Fixture f;
    f.channel.disabledEntries = core::makeError(core::Error::Code::Io, kRawDaemonUnavailable);
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    window.show();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    const QString sentence = QString::fromUtf8(tests::kDevmgrdUnreachableSentence);
    for (QAction* action : {window.toggleAction(), window.bindAction(), window.unbindAction()}) {
        EXPECT_FALSE(action->isEnabled()) << action->text().toStdString();
        EXPECT_TRUE(action->isVisible()) << "a blocked verb must stay visible (§5.3)";
        EXPECT_EQ(action->toolTip(), sentence) << action->text().toStdString();
        // The reason is never the raw diagnostic and never a generic failure.
        EXPECT_FALSE(action->toolTip().contains(QString::fromStdString(kRawDaemonUnavailable)));
        EXPECT_FALSE(action->toolTip().contains(QStringLiteral("Operation failed")));
    }

    window.tabs()->setCurrentIndex(1);
    EXPECT_FALSE(window.loadModuleAction()->isEnabled());
    EXPECT_TRUE(window.loadModuleAction()->isVisible());
    EXPECT_EQ(window.loadModuleAction()->toolTip(), sentence);

    window.tabs()->setCurrentIndex(3);
    for (QAction* action : {window.createSnapshotAction(), window.restoreSnapshotAction(),
                            window.deleteSnapshotAction()}) {
        EXPECT_FALSE(action->isEnabled()) << action->text().toStdString();
        EXPECT_TRUE(action->isVisible()) << "a blocked verb must stay visible (§5.3)";
        EXPECT_EQ(action->toolTip(), sentence);
    }
    // History is a local view toggle over rows already on screen — no daemon
    // needed, so a degraded daemon must NOT disable it.
    EXPECT_TRUE(window.historySnapshotAction()->isEnabled());
    EXPECT_TRUE(window.historySnapshotAction()->isVisible());
}

TEST(MainWindowTest, HealthyDaemonLeavesVerbsEnabledAndUnexplained) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    auto window = f.makeWindow();
    window.show();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    EXPECT_TRUE(window.toggleAction()->isEnabled());
    EXPECT_TRUE(window.toggleAction()->isVisible());
    // QAction::toolTip() falls back to text() when unset, so "no reason attached"
    // is "the tooltip is not the availability sentence", not "the tooltip is empty".
    EXPECT_NE(window.toggleAction()->toolTip(),
              QString::fromUtf8(tests::kDevmgrdUnreachableSentence));
    window.tabs()->setCurrentIndex(3);
    EXPECT_TRUE(window.createSnapshotAction()->isEnabled());
    EXPECT_TRUE(window.createSnapshotAction()->isVisible());
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

    // Off the Snapshots tab (Devices): every verb disabled AND absent — a verb
    // that cannot apply here is not left standing greyed (DESIGN.md §5.3).
    for (QAction* action : {window.createSnapshotAction(), window.restoreSnapshotAction(),
                            window.deleteSnapshotAction(), window.diffSnapshotAction(),
                            window.historySnapshotAction()}) {
        EXPECT_FALSE(action->isEnabled()) << action->text().toStdString();
        EXPECT_FALSE(action->isVisible()) << action->text().toStdString();
    }

    window.tabs()->setCurrentIndex(3);
    // On the tab, the verbs are live; the per-selection refusal is enforced on
    // click (TUI parity), not by greying the action out.
    for (QAction* action : {window.createSnapshotAction(), window.restoreSnapshotAction(),
                            window.deleteSnapshotAction(), window.diffSnapshotAction(),
                            window.historySnapshotAction()}) {
        EXPECT_TRUE(action->isEnabled()) << action->text().toStdString();
        EXPECT_TRUE(action->isVisible()) << action->text().toStdString();
    }
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

// ---- Accessibility pass (beta-06 task 3.5, DESIGN.md §10) ----

// The window enforces the DESIGN.md §3.1 usable-at-800x520 floor so primary
// controls can never be squeezed off-screen.
TEST(MainWindowTest, MinimumWindowSizeMeetsDesignFloor) {
    Fixture f;
    auto window = f.makeWindow();
    EXPECT_EQ(window.minimumSize(), QSize(800, 520));
}

// Every focusable, otherwise-unlabelled control announces a meaningful name to
// assistive technology (DESIGN.md §10 accessible names).
TEST(MainWindowTest, FocusableControlsCarryAccessibleNames) {
    Fixture f;
    auto window = f.makeWindow();
    EXPECT_FALSE(window.filterEdit()->accessibleName().isEmpty());
    EXPECT_FALSE(window.listView()->accessibleName().isEmpty());
    EXPECT_FALSE(window.detailTree()->accessibleName().isEmpty());
    EXPECT_FALSE(window.moduleFilterEdit()->accessibleName().isEmpty());
    EXPECT_FALSE(window.modulesView()->accessibleName().isEmpty());
    EXPECT_FALSE(window.moduleDetailTree()->accessibleName().isEmpty());
    EXPECT_FALSE(window.updatesView()->accessibleName().isEmpty());
    EXPECT_FALSE(window.updatesDetailTree()->accessibleName().isEmpty());
    EXPECT_FALSE(window.snapshotsView()->accessibleName().isEmpty());
    EXPECT_FALSE(window.snapshotsDetailTree()->accessibleName().isEmpty());
    EXPECT_FALSE(window.snapshotFilterEdit()->accessibleName().isEmpty());
}

// Primary verbs carry keyboard shortcuts and each tab is reachable by Ctrl+N;
// keyboard-only operation (DESIGN.md §10) does not depend on the toolbar.
TEST(MainWindowTest, PrimaryVerbsAndTabSwitchingHaveShortcuts) {
    Fixture f;
    auto window = f.makeWindow();
    EXPECT_EQ(window.refreshAction()->shortcut(), QKeySequence(QKeySequence::Refresh));
    EXPECT_EQ(window.toggleAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_E));
    EXPECT_EQ(window.loadModuleAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_L));
    EXPECT_EQ(window.createSnapshotAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_N));

    // One Ctrl+<n> tab-jump per tab, wired as window-owned QShortcuts.
    const auto shortcuts = window.findChildren<QShortcut*>();
    QList<QKeySequence> keys;
    for (const QShortcut* s : shortcuts) keys.push_back(s->key());
    EXPECT_EQ(window.tabs()->count(), 4);
    for (int i = 0; i < window.tabs()->count(); ++i)
        EXPECT_TRUE(keys.contains(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i))))
            << "missing Ctrl+" << (i + 1) << " tab shortcut";
}

// A device name too wide for the row elides in the list (ElideRight, no wrap),
// but the full value stays reachable in the detail pane (DESIGN.md §2.4).
TEST(MainWindowTest, ListRowsElideLongValuesButDetailKeepsFullText) {
    Fixture f;
    const std::string longName(120, 'X');
    f.pal.seedDevice(dev("u1", core::BusType::Usb, longName));
    auto window = f.makeWindow();
    f.refreshAndPump();

    for (QListView* view :
         {window.listView(), window.modulesView(), window.updatesView(), window.snapshotsView()}) {
        EXPECT_EQ(view->textElideMode(), Qt::ElideRight);
        EXPECT_FALSE(view->wordWrap());
    }

    const int row = f.firstDeviceRow();
    ASSERT_GE(row, 0);
    window.listView()->setCurrentIndex(window.listView()->model()->index(row, 0));
    ASSERT_GE(window.detailTree()->topLevelItemCount(), 1);
    EXPECT_EQ(window.detailTree()->topLevelItem(0)->text(1),
              QString::fromStdString(longName));  // full value, not elided
}

// ----- Contextual toolbar: only the active tab's verbs are present ----------
//
// Before this, all fourteen verbs stood on every tab and `disabled` meant both
// "wrong tab" and "applies here, cannot run right now". The second meaning is
// the one carrying the shared unavailability sentence and the guard reasons, so
// the first had to go (DESIGN.md §5.3).

namespace {
// The toolbar exactly as the user sees it on the active tab: separators as `|`,
// actions as their visible text, in toolbar order.
QStringList visibleToolbarEntries(const gui::MainWindow& window) {
    QStringList entries;
    for (const QAction* action : window.toolbar()->actions()) {
        if (!action->isVisible()) continue;
        entries.push_back(action->isSeparator() ? QStringLiteral("|") : action->text());
    }
    return entries;
}

// The same list with the separators dropped — the verb set and its order.
QStringList visibleVerbs(const gui::MainWindow& window) {
    QStringList verbs = visibleToolbarEntries(window);
    verbs.removeAll(QStringLiteral("|"));
    return verbs;
}

std::vector<QAction*> allVerbs(const gui::MainWindow& window) {
    return {window.refreshAction(),
            window.toggleAction(),
            window.bindAction(),
            window.unbindAction(),
            window.loadModuleAction(),
            window.unloadModuleAction(),
            window.refreshUpdatesAction(),
            window.installUpdateAction(),
            window.dismissRequestAction(),
            window.createSnapshotAction(),
            window.restoreSnapshotAction(),
            window.diffSnapshotAction(),
            window.historySnapshotAction(),
            window.deleteSnapshotAction()};
}
}  // namespace

TEST(MainWindowTest, ToolbarShowsOnlyTheActiveTabsVerbsInOrder) {
    Fixture f;
    auto window = f.makeWindow();

    window.tabs()->setCurrentIndex(0);
    EXPECT_EQ(
        visibleVerbs(window),
        QStringList({QStringLiteral("Refresh"), QStringLiteral("Disable"),
                     QStringLiteral("Bind driver…"), QStringLiteral("Unbind driver (advanced)")}));

    window.tabs()->setCurrentIndex(1);
    EXPECT_EQ(visibleVerbs(window),
              QStringList({QStringLiteral("Load Module…"), QStringLiteral("Unload")}));

    // No dismissible request yet, so `Dismiss Request` has no object at all.
    window.tabs()->setCurrentIndex(2);
    EXPECT_EQ(visibleVerbs(window),
              QStringList({QStringLiteral("Refresh Updates"), QStringLiteral("Install Update")}));

    window.tabs()->setCurrentIndex(3);
    EXPECT_EQ(visibleVerbs(window),
              QStringList({QStringLiteral("Create Snapshot…"), QStringLiteral("Restore Snapshot…"),
                           QStringLiteral("Diff Snapshot"), QStringLiteral("History"),
                           QStringLiteral("Delete Snapshot")}));

    // And back: switching tabs re-composes the one toolbar, it does not rebuild it.
    window.tabs()->setCurrentIndex(0);
    EXPECT_EQ(visibleVerbs(window).front(), QStringLiteral("Refresh"));
}

// A foreign verb is ABSENT, and also inert: hidden alone would leave its
// shortcut and its context-menu entry live on the wrong view.
TEST(MainWindowTest, ForeignVerbsAreHiddenAndDisabledOnEveryTab) {
    Fixture f;
    auto window = f.makeWindow();

    for (int tab = 0; tab < window.tabs()->count(); ++tab) {
        window.tabs()->setCurrentIndex(tab);
        for (QAction* action : allVerbs(window)) {
            if (action->data().toInt() == tab) continue;
            EXPECT_FALSE(action->isVisible())
                << action->text().toStdString() << " visible on tab " << tab;
            EXPECT_FALSE(action->isEnabled())
                << action->text().toStdString() << " enabled on tab " << tab;
        }
    }
}

TEST(MainWindowTest, ToolbarSeparatorsAreNeverOrphaned) {
    Fixture f;
    auto window = f.makeWindow();

    for (int tab = 0; tab < window.tabs()->count(); ++tab) {
        window.tabs()->setCurrentIndex(tab);
        const QStringList entries = visibleToolbarEntries(window);
        ASSERT_FALSE(entries.isEmpty()) << "tab " << tab << " shows no toolbar entry at all";
        EXPECT_NE(entries.front(), QStringLiteral("|")) << "leading separator on tab " << tab;
        EXPECT_NE(entries.back(), QStringLiteral("|")) << "trailing separator on tab " << tab;
        for (int i = 1; i < entries.size(); ++i)
            EXPECT_FALSE(entries[i] == QStringLiteral("|") && entries[i - 1] == QStringLiteral("|"))
                << "doubled separator on tab " << tab << " at " << i;
    }
    // The groups themselves are real: Devices separates refresh, enable/disable,
    // the additive bind and the destructive unbind (§5.3, §10).
    window.tabs()->setCurrentIndex(0);
    EXPECT_EQ(
        visibleToolbarEntries(window),
        QStringList({QStringLiteral("Refresh"), QStringLiteral("|"), QStringLiteral("Disable"),
                     QStringLiteral("|"), QStringLiteral("Bind driver…"), QStringLiteral("|"),
                     QStringLiteral("Unbind driver (advanced)")}));
}

// Shortcuts stay bound and become inert off their tab (DESIGN.md §10.1) — the
// binding is never removed and re-added, so it cannot be lost by a mis-ordered
// update.
TEST(MainWindowTest, VerbShortcutsStayBoundAndGoInertOffTab) {
    Fixture f;
    auto window = f.makeWindow();

    for (int tab : {1, 2, 3, 0}) window.tabs()->setCurrentIndex(tab);
    EXPECT_EQ(window.refreshAction()->shortcut(), QKeySequence(QKeySequence::Refresh));
    EXPECT_EQ(window.toggleAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_E));
    EXPECT_EQ(window.loadModuleAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_L));
    EXPECT_EQ(window.createSnapshotAction()->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_N));

    // On Devices, F5's action is live and refreshes devices.
    window.refreshAction()->trigger();
    EXPECT_EQ(f.refreshCalls, 1);

    // On Modules it is hidden and disabled, so the same key cannot refresh a view
    // the user is not looking at (the pre-change behaviour).
    window.tabs()->setCurrentIndex(1);
    EXPECT_FALSE(window.refreshAction()->isVisible());
    EXPECT_FALSE(window.refreshAction()->isEnabled());
    window.refreshAction()->trigger();  // no-ops while disabled
    EXPECT_EQ(f.refreshCalls, 1);
}

// The daemon-backed verbs of the tab the user is standing on stay visible and
// disabled with the shared sentence attached — a refusal is explained, never
// hidden (DESIGN.md §5.3, §6.1).
TEST(MainWindowTest, DaemonDownKeepsTheTabsVerbsVisibleWithTooltips) {
    Fixture f;
    f.pal.seedDevice(dev("u1", core::BusType::Usb, "Mouse"));
    f.channel.disabledEntries = core::makeError(core::Error::Code::Io, kRawDaemonUnavailable);
    f.facade.refresh().wait();
    auto window = f.makeWindow();
    f.refreshAndPump();
    f.selectFirstDevice(window);

    const QString sentence = QString::fromUtf8(tests::kDevmgrdUnreachableSentence);
    struct Case {
        int tab;
        std::vector<QAction*> blocked;
    };
    const std::vector<Case> cases{
        {0, {window.toggleAction(), window.bindAction(), window.unbindAction()}},
        {1, {window.loadModuleAction()}},
        {3,
         {window.createSnapshotAction(), window.restoreSnapshotAction(),
          window.diffSnapshotAction(), window.deleteSnapshotAction()}}};
    for (const auto& c : cases) {
        window.tabs()->setCurrentIndex(c.tab);
        for (QAction* action : c.blocked) {
            EXPECT_TRUE(action->isVisible())
                << action->text().toStdString() << " hidden while devmgrd is down";
            EXPECT_FALSE(action->isEnabled()) << action->text().toStdString();
            EXPECT_TRUE(action->toolTip().contains(sentence))
                << action->text().toStdString() << " tooltip: " << action->toolTip().toStdString();
        }
    }

    // Reads stay usable while the daemon is degraded (§6), so Devices keeps a
    // live Refresh rather than a greyed one.
    window.tabs()->setCurrentIndex(0);
    EXPECT_TRUE(window.refreshAction()->isVisible());
    EXPECT_TRUE(window.refreshAction()->isEnabled());
}

// `Dismiss Request` is the one verb whose object can be missing on its own tab:
// with no request there is nothing to dismiss, which is inapplicability rather
// than a refusal, so it is hidden and comes back with the request.
TEST(MainWindowTest, DismissRequestAppearsOnlyWithADismissibleRequest) {
    Fixture f;
    auto window = f.makeWindow();
    window.tabs()->setCurrentIndex(2);
    EXPECT_FALSE(window.dismissRequestAction()->isVisible());
    EXPECT_FALSE(window.dismissRequestAction()->isEnabled());
    EXPECT_FALSE(visibleToolbarEntries(window).back() == QStringLiteral("|"));

    f.bus.publish(core::UpdateRequestEvent{.providerId = "fake",
                                           .deviceId = "a1",
                                           .kind = "post",
                                           .message = "unplug and replug the device"});
    EXPECT_TRUE(window.dismissRequestAction()->isVisible());
    EXPECT_TRUE(window.dismissRequestAction()->isEnabled());
    EXPECT_EQ(visibleVerbs(window),
              QStringList({QStringLiteral("Refresh Updates"), QStringLiteral("Install Update"),
                           QStringLiteral("Dismiss Request")}));

    window.dismissRequestAction()->trigger();
    EXPECT_FALSE(window.dismissRequestAction()->isVisible());
    const QStringList after = visibleToolbarEntries(window);
    EXPECT_NE(after.back(), QStringLiteral("|"));  // its group separator went with it
}

// Icons supplement the label, never replace it: destructive and advanced verbs
// stay text-labeled and every action's visible text remains its accessible name
// (DESIGN.md §4.4, §10.1).
TEST(MainWindowTest, ToolbarKeepsTextBesideAnyIcon) {
    Fixture f;
    auto window = f.makeWindow();

    EXPECT_EQ(window.toolbar()->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    for (int tab = 0; tab < window.tabs()->count(); ++tab) {
        window.tabs()->setCurrentIndex(tab);
        for (const QString& verb : visibleVerbs(window)) EXPECT_FALSE(verb.isEmpty());
    }
}
