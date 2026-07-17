#pragma once
#include <functional>
#include <future>
#include <string>
#include <vector>

#include <QMainWindow>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/snapshots_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "devmgr/app/updates_vm.hpp"
#include "devmgr/runtime/event_bus.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/module_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"
#include "gui/src/snapshot_list_model.hpp"
#include "gui/src/update_list_model.hpp"

class QAction;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QListView;
class QTabWidget;
class QTreeWidget;

namespace devmgr::gui {

// Strict-parity main window: a QTabWidget central (Devices | Modules |
// Updates | Snapshots). The Devices page keeps the Phase 3 spec shape (filter
// box + grouped device list left, key/value detail pane right); the Modules
// page mirrors the T11 TUI screen (Secure Boot banner + filter + fixed-column
// module list left, module detail pane right); the Updates page mirrors the
// T11 TUI Updates screen (availability/reboot/Secure Boot banner + durable
// request banner + fixed-column update list left, update detail pane right);
// the Snapshots page mirrors the Phase 7 TUI Snapshots screen (counts banner +
// fixed-column snapshot list left, snapshot detail pane right).
// The shared ViewModels remain the source of truth; widgets mirror them. All
// Devices/Modules commands are injected through Actions so the composition
// root keeps owning future custody; Updates and Snapshots commands call the
// facade directly and this window keeps its own future custody (pending_,
// drained in the destructor) — the drain-before-teardown contract in
// tui/src/tui_app.cpp applies identically to the GUI root either way.
class MainWindow final : public QMainWindow {
    Q_OBJECT
   public:
    // Injected command callbacks plus modal seams. confirm/textInput/confirmQuit
    // empty ({}) fall back to the real QMessageBox / QInputDialog; tests inject
    // deterministic answers instead.
    struct Actions {
        std::function<void()> onRefresh;
        std::function<void(const core::DeviceId&, bool)> onSetEnabled;
        std::function<void(const std::string&)> onLoadModule;
        std::function<void(const std::string&)> onUnloadModule;
        std::function<void(const core::DeviceId&, const std::string&)> onBindDriver;
        std::function<void(const core::DeviceId&)> onUnbindDriver;
        std::function<bool(const QString&)> confirm;                       // {} => QMessageBox
        std::function<QString(const QString&, const QString&)> textInput;  // {} => QInputDialog
        std::function<bool(const QString&)> confirmQuit;  // {} => QMessageBox (close guard)
    };

    MainWindow(app::ApplicationFacade& facade, app::DeviceListVM& listVm,
               app::DeviceDetailVM& detailVm, app::StatusLineVM& statusVm,
               app::ModulesVM& modulesVm, app::UpdatesVM& updatesVm, app::SnapshotsVM& snapshotsVm,
               QtUiDispatcher& dispatcher, runtime::EventBus& bus, Actions actions,
               QWidget* parent = nullptr);
    ~MainWindow() override;

    // Test accessors (offscreen tests drive/inspect the real widgets).
    QListView* listView() const { return listView_; }
    QTreeWidget* detailTree() const { return detailTree_; }
    QLineEdit* filterEdit() const { return filterEdit_; }
    QAction* toggleAction() const { return toggleAction_; }
    QTabWidget* tabs() const { return tabs_; }
    QListView* modulesView() const { return modulesView_; }
    QTreeWidget* moduleDetailTree() const { return moduleDetailTree_; }
    QLineEdit* moduleFilterEdit() const { return moduleFilterEdit_; }
    QLabel* bannerLabel() const { return bannerLabel_; }
    QAction* loadModuleAction() const { return loadModuleAction_; }
    QAction* unloadModuleAction() const { return unloadModuleAction_; }
    QAction* unbindAction() const { return unbindAction_; }
    QAction* bindAction() const { return bindAction_; }
    QListView* updatesView() const { return updatesView_; }
    QTreeWidget* updatesDetailTree() const { return updatesDetailTree_; }
    QLabel* updatesBannerLabel() const { return updatesBannerLabel_; }
    QLabel* requestBannerLabel() const { return requestBannerLabel_; }
    QAction* installUpdateAction() const { return installUpdateAction_; }
    QAction* refreshUpdatesAction() const { return refreshUpdatesAction_; }
    QAction* dismissRequestAction() const { return dismissRequestAction_; }
    QListView* snapshotsView() const { return snapshotsView_; }
    QTreeWidget* snapshotsDetailTree() const { return snapshotsDetailTree_; }
    QLabel* snapshotsBannerLabel() const { return snapshotsBannerLabel_; }
    QAction* createSnapshotAction() const { return createSnapshotAction_; }
    QAction* restoreSnapshotAction() const { return restoreSnapshotAction_; }
    QAction* deleteSnapshotAction() const { return deleteSnapshotAction_; }

   protected:
    void closeEvent(QCloseEvent* event) override;  // spec §5.5 quit guard: installActive()

   private:
    bool askConfirm(const QString& prompt);
    void syncSelectionFromVm();  // after modelReset: VM re-resolved by DeviceId
    void updateDetailPane();
    void updateModuleDetailPane();
    void updateUpdatesDetailPane();
    void updateSnapshotsDetailPane();
    void updateStatusBar();
    void updateActionEnablement();                  // tab-aware; folds the old updateToggleAction()
    void updateRequestBannerLabel();                // requestBanner() text + visibility
    void pruneAndPushPending(std::future<void> f);  // this window's own future custody

    app::ApplicationFacade& facade_;
    app::DeviceListVM& listVm_;
    app::DeviceDetailVM& detailVm_;
    app::StatusLineVM& statusVm_;
    app::ModulesVM& modulesVm_;
    app::UpdatesVM& updatesVm_;
    app::SnapshotsVM& snapshotsVm_;
    runtime::EventBus& bus_;
    Actions actions_;
    DeviceListModel* model_ = nullptr;  // Qt-parented to this window
    QListView* listView_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QTreeWidget* detailTree_ = nullptr;
    QAction* toggleAction_ = nullptr;
    ModuleListModel* moduleModel_ = nullptr;  // Qt-parented to this window
    QTabWidget* tabs_ = nullptr;
    QListView* modulesView_ = nullptr;
    QLineEdit* moduleFilterEdit_ = nullptr;
    QLabel* bannerLabel_ = nullptr;
    QTreeWidget* moduleDetailTree_ = nullptr;
    QAction* loadModuleAction_ = nullptr;
    QAction* unloadModuleAction_ = nullptr;
    QAction* unbindAction_ = nullptr;
    QAction* bindAction_ = nullptr;
    UpdateListModel* updateModel_ = nullptr;  // Qt-parented to this window
    QListView* updatesView_ = nullptr;
    QTreeWidget* updatesDetailTree_ = nullptr;
    QLabel* updatesBannerLabel_ = nullptr;
    QLabel* requestBannerLabel_ = nullptr;
    QAction* installUpdateAction_ = nullptr;
    QAction* refreshUpdatesAction_ = nullptr;
    QAction* dismissRequestAction_ = nullptr;
    SnapshotListModel* snapshotModel_ = nullptr;  // Qt-parented to this window
    QListView* snapshotsView_ = nullptr;
    QTreeWidget* snapshotsDetailTree_ = nullptr;
    QLabel* snapshotsBannerLabel_ = nullptr;
    QAction* createSnapshotAction_ = nullptr;
    QAction* restoreSnapshotAction_ = nullptr;
    QAction* deleteSnapshotAction_ = nullptr;
    // Future custody for Updates actions (refreshUpdates/installUpdate), called
    // directly on facade_ rather than through an injected Actions callback:
    // ApplicationFacade's documented lifetime contract requires every handle be
    // waited before the facade dies, so this window drains its own set in its
    // destructor rather than routing through the composition root's `pending`
    // (the pattern gui_app.cpp uses for Devices/Modules mutations).
    std::vector<std::future<void>> pending_;
};

}  // namespace devmgr::gui
