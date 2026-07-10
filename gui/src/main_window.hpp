#pragma once
#include <functional>
#include <string>

#include <QMainWindow>

#include "devmgr/app/application_facade.hpp"
#include "devmgr/app/device_detail_vm.hpp"
#include "devmgr/app/device_list_vm.hpp"
#include "devmgr/app/modules_vm.hpp"
#include "devmgr/app/status_line_vm.hpp"
#include "gui/src/device_list_model.hpp"
#include "gui/src/module_list_model.hpp"
#include "gui/src/qt_ui_dispatcher.hpp"

class QAction;
class QLabel;
class QLineEdit;
class QListView;
class QTabWidget;
class QTreeWidget;

namespace devmgr::gui {

// Strict-parity main window: a QTabWidget central (Devices | Modules). The
// Devices page keeps the Phase 3 spec shape (filter box + grouped device list
// left, key/value detail pane right); the Modules page mirrors the T11 TUI
// screen (Secure Boot banner + filter + fixed-column module list left, module
// detail pane right). The shared ViewModels remain the source of truth;
// widgets mirror them. All commands are injected through Actions so the
// composition root keeps owning future custody (the drain-before-teardown
// contract in tui/src/tui_app.cpp applies identically to the GUI root).
class MainWindow final : public QMainWindow {
    Q_OBJECT
   public:
    // Injected command callbacks plus modal seams. confirm/textInput empty
    // ({}) fall back to the real QMessageBox / QInputDialog; tests inject
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
    };

    MainWindow(app::ApplicationFacade& facade, app::DeviceListVM& listVm,
               app::DeviceDetailVM& detailVm, app::StatusLineVM& statusVm,
               app::ModulesVM& modulesVm, QtUiDispatcher& dispatcher, Actions actions,
               QWidget* parent = nullptr);

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

   private:
    bool askConfirm(const QString& prompt);
    void syncSelectionFromVm();  // after modelReset: VM re-resolved by DeviceId
    void updateDetailPane();
    void updateModuleDetailPane();
    void updateStatusBar();
    void updateActionEnablement();  // tab-aware; folds the old updateToggleAction()

    app::ApplicationFacade& facade_;
    app::DeviceListVM& listVm_;
    app::DeviceDetailVM& detailVm_;
    app::StatusLineVM& statusVm_;
    app::ModulesVM& modulesVm_;
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
};

}  // namespace devmgr::gui
